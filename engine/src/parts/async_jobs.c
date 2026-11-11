#include "async_jobs.h"

#include <SDL.h>
#include <stdlib.h>
#include <string.h>

enum parts_async_state {
	PARTS_ASYNC_QUEUED,
	PARTS_ASYNC_RUNNING,
	PARTS_ASYNC_DONE,
};

struct parts_async_job {
	uint64_t id;
	void *payload;
	bool (*prepare)(void *payload);
	enum parts_async_state state;
	bool ok;
	bool cancelled;
	struct parts_async_job *next_all;
	struct parts_async_job *next_queue;
};

struct parts_async_executor {
	SDL_mutex *mutex;
	SDL_cond *cond;
	SDL_Thread *thread;
	struct parts_async_job *all;
	struct parts_async_job *queue_head;
	struct parts_async_job *queue_tail;
	unsigned limit;
	unsigned count;
	uint64_t next_id;
	bool stop;
};

static void queue_remove(struct parts_async_executor *executor,
		struct parts_async_job *job)
{
	struct parts_async_job *prev = NULL;
	struct parts_async_job *cur = executor->queue_head;
	while (cur && cur != job) {
		prev = cur;
		cur = cur->next_queue;
	}
	if (!cur)
		return;
	if (prev)
		prev->next_queue = cur->next_queue;
	else
		executor->queue_head = cur->next_queue;
	if (executor->queue_tail == cur)
		executor->queue_tail = prev;
	cur->next_queue = NULL;
}

static int async_worker(void *data)
{
	struct parts_async_executor *executor = data;
	for (;;) {
		if (SDL_LockMutex(executor->mutex) != 0)
			return 0;
		while (!executor->stop && !executor->queue_head) {
			if (SDL_CondWait(executor->cond, executor->mutex) != 0) {
				SDL_UnlockMutex(executor->mutex);
				return 0;
			}
		}
		if (executor->stop) {
			SDL_UnlockMutex(executor->mutex);
			break;
		}

		struct parts_async_job *job = executor->queue_head;
		executor->queue_head = job->next_queue;
		if (!executor->queue_head)
			executor->queue_tail = NULL;
		job->next_queue = NULL;
		job->state = PARTS_ASYNC_RUNNING;
		bool (*prepare)(void *) = job->prepare;
		void *payload = job->payload;
		SDL_UnlockMutex(executor->mutex);

		bool ok = prepare ? prepare(payload) : false;

		if (SDL_LockMutex(executor->mutex) != 0)
			return 0;
		job->ok = ok;
		job->state = PARTS_ASYNC_DONE;
		SDL_UnlockMutex(executor->mutex);
	}
	return 0;
}

struct parts_async_executor *parts_async_executor_new(unsigned limit)
{
	if (limit < 1 || limit > 64)
		return NULL;

	struct parts_async_executor *executor = calloc(1, sizeof(*executor));
	if (!executor)
		return NULL;
	executor->limit = limit;

	executor->mutex = SDL_CreateMutex();
	if (!executor->mutex) {
		free(executor);
		return NULL;
	}
	executor->cond = SDL_CreateCond();
	if (!executor->cond) {
		SDL_DestroyMutex(executor->mutex);
		free(executor);
		return NULL;
	}
	executor->thread = SDL_CreateThread(async_worker, "parts-async", executor);
	if (!executor->thread) {
		SDL_DestroyCond(executor->cond);
		SDL_DestroyMutex(executor->mutex);
		free(executor);
		return NULL;
	}
	return executor;
}

uint64_t parts_async_submit(struct parts_async_executor *executor, void *payload,
		bool (*prepare)(void *payload))
{
	if (!executor || !prepare)
		return 0;

	struct parts_async_job *job = calloc(1, sizeof(*job));
	if (!job)
		return 0;

	if (SDL_LockMutex(executor->mutex) != 0) {
		free(job);
		return 0;
	}
	if (executor->stop || executor->count >= executor->limit ||
			executor->next_id == UINT64_MAX) {
		SDL_UnlockMutex(executor->mutex);
		free(job);
		return 0;
	}

	uint64_t id = ++executor->next_id;
	job->id = id;
	job->payload = payload;
	job->prepare = prepare;
	job->state = PARTS_ASYNC_QUEUED;
	job->next_all = executor->all;
	executor->all = job;
	if (executor->queue_tail)
		executor->queue_tail->next_queue = job;
	else
		executor->queue_head = job;
	executor->queue_tail = job;
	executor->count++;
	SDL_CondSignal(executor->cond);
	SDL_UnlockMutex(executor->mutex);
	return id;
}

bool parts_async_busy(struct parts_async_executor *executor)
{
	if (!executor)
		return false;
	if (SDL_LockMutex(executor->mutex) != 0)
		return false;
	bool busy = executor->count > 0;
	SDL_UnlockMutex(executor->mutex);
	return busy;
}

bool parts_async_queue_full(struct parts_async_executor *executor)
{
    if (!executor || SDL_LockMutex(executor->mutex) != 0)
        return false; /* Let submit report a real executor failure directly. */
    bool full = !executor->stop && executor->count >= executor->limit;
    SDL_UnlockMutex(executor->mutex);
    return full;
}

bool parts_async_cancel(struct parts_async_executor *executor, uint64_t id)
{
	if (!executor)
		return false;
	if (SDL_LockMutex(executor->mutex) != 0)
		return false;

	struct parts_async_job *job = executor->all;
	while (job && job->id != id)
		job = job->next_all;
	if (!job) {
		SDL_UnlockMutex(executor->mutex);
		return false;
	}

	job->cancelled = true;
	if (job->state == PARTS_ASYNC_QUEUED) {
		queue_remove(executor, job);
		job->state = PARTS_ASYNC_DONE;
		job->ok = false;
	}
	SDL_CondBroadcast(executor->cond);
	SDL_UnlockMutex(executor->mutex);
	return true;
}

bool parts_async_take(struct parts_async_executor *executor,
		struct parts_async_result *out)
{
	if (!executor || !out)
		return false;
	if (SDL_LockMutex(executor->mutex) != 0)
		return false;

	struct parts_async_job **link = &executor->all;
	struct parts_async_job *job = NULL;
	while (*link) {
		if ((*link)->state == PARTS_ASYNC_DONE) {
			job = *link;
			*link = job->next_all;
			break;
		}
		link = &(*link)->next_all;
	}
	if (!job) {
		SDL_UnlockMutex(executor->mutex);
		return false;
	}

	struct parts_async_result result = {
		.id = job->id,
		.payload = job->payload,
		.ok = job->ok,
		.cancelled = job->cancelled,
	};
	if (executor->count > 0)
		executor->count--;
	SDL_UnlockMutex(executor->mutex);

	free(job);
	*out = result;
	return true;
}

void parts_async_executor_free(struct parts_async_executor *executor,
		void (*dispose)(void *payload))
{
	if (!executor)
		return;

	SDL_LockMutex(executor->mutex);
	executor->stop = true;
	for (struct parts_async_job *job = executor->all; job; job = job->next_all)
		job->cancelled = true;
	SDL_CondBroadcast(executor->cond);
	SDL_UnlockMutex(executor->mutex);

	SDL_WaitThread(executor->thread, NULL);

	SDL_LockMutex(executor->mutex);
	struct parts_async_job *jobs = executor->all;
	executor->all = NULL;
	executor->queue_head = NULL;
	executor->queue_tail = NULL;
	executor->count = 0;
	SDL_UnlockMutex(executor->mutex);

	while (jobs) {
		struct parts_async_job *next = jobs->next_all;
		if (dispose)
			dispose(jobs->payload);
		free(jobs);
		jobs = next;
	}

	SDL_DestroyCond(executor->cond);
	SDL_DestroyMutex(executor->mutex);
	free(executor);
}
