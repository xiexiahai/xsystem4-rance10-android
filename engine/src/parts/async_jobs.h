#ifndef PARTS_ASYNC_JOBS_H
#define PARTS_ASYNC_JOBS_H

#include <stdbool.h>
#include <stdint.h>

struct parts_async_executor;

struct parts_async_result {
	uint64_t id;
	void *payload;
	bool ok;
	bool cancelled;
};

struct parts_async_executor *parts_async_executor_new(unsigned limit);
uint64_t parts_async_submit(struct parts_async_executor *executor, void *payload,
		bool (*prepare)(void *payload));
bool parts_async_busy(struct parts_async_executor *executor);
/* DONE jobs retain their queue credit until taken by the main thread. */
bool parts_async_queue_full(struct parts_async_executor *executor);
bool parts_async_cancel(struct parts_async_executor *executor, uint64_t id);
/* Main consumer only. Cancel id and transfer a queued/done result immediately.
 * A running payload remains owned by its worker and is taken normally later.
 * True means out received ownership; on false out is left unchanged. */
bool parts_async_cancel_take(struct parts_async_executor *executor, uint64_t id,
		struct parts_async_result *out);
bool parts_async_take(struct parts_async_executor *executor,
		struct parts_async_result *out);
void parts_async_executor_free(struct parts_async_executor *executor,
		void (*dispose)(void *payload));

#endif /* PARTS_ASYNC_JOBS_H */
