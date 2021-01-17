#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include "queue.h"

struct queue_node {
	uint8_t *buffer;
	int len;
	struct queue_node *next;
};

struct queue_t {
	struct queue_node *first_free_node;
	struct queue_node *last_free_node;

	struct queue_node *first_filled_node;
	struct queue_node *last_filled_node;

	struct queue_node *detached_node;

	pthread_mutex_t mutex;
	pthread_cond_t condition;
};

void free_nodes(struct queue_node *nodes) {
	struct queue_node *cur_node = nodes;
	while (cur_node != NULL) {
		struct queue_node *next = cur_node->next;
		if (cur_node->buffer != NULL) {
			free(cur_node->buffer);
		}
		free(cur_node);
		cur_node = next;
	}
}

int create_queue(uint32_t buffer_size, int capacity, queue **queue) {
	struct queue_t *result = malloc(sizeof(struct queue_t));
	if (result == NULL) {
		return -ENOMEM;
	}

	struct queue_node *first_node = NULL;
	struct queue_node *last_node = NULL;
	for (int i = 0; i < capacity; i++) {
		struct queue_node *cur = malloc(sizeof(struct queue_node));
		if (cur == NULL) {
			free_nodes(first_node);
			free(result);
			return -ENOMEM;
		}
		cur->buffer = malloc(sizeof(uint8_t) * buffer_size);
		cur->next = NULL;
		cur->len = 0;
		if (cur->buffer == NULL) {
			free_nodes(first_node);
			free(cur);
			free(result);
			return -ENOMEM;
		}
		if (last_node == NULL) {
			first_node = cur;
		} else {
			last_node->next = cur;
		}
		last_node = cur;
	}

	result->first_free_node = first_node;
	result->last_free_node = last_node;
	result->first_filled_node = NULL;
	result->last_filled_node = NULL;
	result->condition = (pthread_cond_t )PTHREAD_COND_INITIALIZER;

	*queue = result;
	return 0;
}

void put(const uint8_t *buffer, const int len, queue *queue) {
	pthread_mutex_lock(&queue->mutex);
	struct queue_node *to_fill;
	if (queue->first_free_node == NULL) {
		// queue is full
		// overwrite last node
		to_fill = queue->last_filled_node;
	} else {
		// remove from free nodes pool
		to_fill = queue->first_free_node;
		queue->first_free_node = queue->first_free_node->next;
		to_fill->next = NULL;
		if (queue->first_free_node == NULL) {
			queue->last_free_node = NULL;
		}

		// add to filled nodes pool
		if (queue->last_filled_node == NULL) {
			queue->first_filled_node = to_fill;
		} else {
			queue->last_filled_node->next = to_fill;
		}
		queue->last_filled_node = to_fill;
	}

	memcpy(to_fill->buffer, buffer, len);
	to_fill->len = len;
	pthread_cond_broadcast(&queue->condition);

	pthread_mutex_unlock(&queue->mutex);
}

void take_buffer_for_processing(uint8_t **buffer, int *len, queue *queue) {
	pthread_mutex_lock(&queue->mutex);
	// "while" loop is for spurious wakeups
	while (queue->first_filled_node == NULL) {
		pthread_cond_wait(&queue->condition, &queue->mutex);
	}
	// the idea is to keep a node that being processed in the detached mode
	// i.e. input thread cannot read or write into it, while buffer is being sent to client/file (which can be slow)
	// it would allow non-mutex access to such buffer
	// and as a result small synchornization section
	// and as a result faster concurrency
	queue->detached_node = queue->first_filled_node;
	queue->first_filled_node = queue->first_filled_node->next;
	queue->detached_node->next = NULL;
	if (queue->first_filled_node == NULL) {
		queue->last_filled_node = NULL;
	}
	*buffer = queue->detached_node->buffer;
	*len = queue->detached_node->len;
	pthread_mutex_unlock(&queue->mutex);
}

void complete_buffer_processing(queue *queue) {
	pthread_mutex_lock(&queue->mutex);
	if (queue->last_free_node == NULL) {
		queue->first_free_node = queue->detached_node;
	} else {
		queue->last_free_node->next = queue->detached_node;
	}
	queue->last_free_node = queue->detached_node;
	queue->detached_node = NULL;
	pthread_mutex_unlock(&queue->mutex);
}

void destroy_queue(queue *queue) {
	if (queue == NULL) {
		return;
	}
	free_nodes(queue->first_free_node);
	free_nodes(queue->first_filled_node);
	if (queue->detached_node != NULL) {
		free(queue->detached_node->buffer);
		free(queue->detached_node);
	}
	free(queue);
}
