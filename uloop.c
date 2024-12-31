/*
 * uloop - event loop implementation
 *
 * Copyright (C) 2010-2016 Felix Fietkau <nbd@openwrt.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/time.h>
#include <sys/types.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <fcntl.h>
#include <stdbool.h>
#include <limits.h>

#include "uloop.h"
#include "utils.h"

#ifdef USE_KQUEUE
#include <sys/event.h>
#endif
#ifdef USE_EPOLL
#include <sys/epoll.h>
#include <sys/timerfd.h>
#endif
#include <sys/wait.h>

struct uloop_fd_event {
	struct uloop_fd *fd;
	unsigned int events;
};

struct uloop_fd_stack {
	struct uloop_fd_stack *next;
	struct uloop_fd *fd;
	unsigned int events;
};

static struct uloop_fd_stack *fd_stack = NULL;

#define ULOOP_MAX_EVENTS 10

static struct list_head timeouts = LIST_HEAD_INIT(timeouts);
static struct list_head processes = LIST_HEAD_INIT(processes);
static struct list_head signals = LIST_HEAD_INIT(signals);

static int poll_fd = -1;
bool uloop_cancelled = false;
bool global_current_uloop_timeout_reached = false;
bool uloop_handle_sigchld = true;
static int uloop_status = 0;
static bool do_sigchld = false;

static struct uloop_fd_event cur_fds[ULOOP_MAX_EVENTS];
static int cur_fd, cur_nfds;
static int uloop_run_depth = 0;

uloop_fd_handler uloop_fd_set_cb = NULL;

int uloop_fd_add(struct uloop_fd *sock, unsigned int flags);

#ifdef USE_KQUEUE
#include "uloop-kqueue.c"
#endif

#ifdef USE_EPOLL
#include "uloop-epoll.c"
#endif

static void set_signo(uint64_t *signums, int signo)
{
	if (signo >= 1 && signo <= 64)
		*signums |= (1u << (signo - 1));
}

static bool get_signo(uint64_t signums, int signo)
{
	return (signo >= 1) && (signo <= 64) && (signums & (1u << (signo - 1)));
}

static void signal_consume(struct uloop_fd *fd, unsigned int events)
{
	struct uloop_signal *usig, *usig_next;
	uint64_t signums = 0;
	uint8_t buf[32];
	ssize_t nsigs;

	do {
		nsigs = read(fd->fd, buf, sizeof(buf));

		for (ssize_t i = 0; i < nsigs; i++)
			set_signo(&signums, buf[i]);
	}
	while (nsigs > 0);

	list_for_each_entry_safe(usig, usig_next, &signals, list)
		if (get_signo(signums, usig->signo))
			usig->cb(usig);
}

static int waker_pipe = -1;
static struct uloop_fd waker_fd = {
	.fd = -1,
	.cb = signal_consume,
};

static void waker_init_fd(int fd)
{
	fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

static int waker_init(void)
{
	int fds[2];

	if (waker_pipe >= 0)
		return 0;

	if (pipe(fds) < 0)
		return -1;

	waker_init_fd(fds[0]);
	waker_init_fd(fds[1]);
	waker_pipe = fds[1];

	waker_fd.fd = fds[0];
	waker_fd.cb = signal_consume;
	uloop_fd_add(&waker_fd, ULOOP_READ);

	return 0;
}

static void uloop_setup_signals(bool add);

int uloop_init(void)
{
	if (uloop_init_pollfd() < 0)
		return -1;

	if (waker_init() < 0) {
		uloop_done();
		return -1;
	}

	uloop_setup_signals(true);

	return 0;
}

static bool uloop_fd_stack_event(struct uloop_fd *fd, int events)
{
	struct uloop_fd_stack *cur;

	/*
	 * Do not buffer events for level-triggered fds, they will keep firing.
	 * Caller needs to take care of recursion issues.
	 */
	if (!(fd->flags & ULOOP_EDGE_TRIGGER))
		return false;

	for (cur = fd_stack; cur; cur = cur->next) {
		if (cur->fd != fd)
			continue;

		if (events < 0)
			cur->fd = NULL;
		else
			cur->events |= events | ULOOP_EVENT_BUFFERED;

		return true;
	}

	return false;
}

static void uloop_run_events(int64_t timeout)
{
	struct uloop_fd_event *cur;
	struct uloop_fd *fd;

	if (!cur_nfds) {
		cur_fd = 0;
		cur_nfds = uloop_fetch_events(timeout);
		if (cur_nfds < 0)
			cur_nfds = 0;
	}

	while (cur_nfds > 0) {
		struct uloop_fd_stack stack_cur;
		unsigned int events;

		cur = &cur_fds[cur_fd++];
		cur_nfds--;

		fd = cur->fd;
		events = cur->events;
		if (!fd)
			continue;

		if (!fd->cb)
			continue;

		if (uloop_fd_stack_event(fd, cur->events))
			continue;

		stack_cur.next = fd_stack;
		stack_cur.fd = fd;
		fd_stack = &stack_cur;
		do {
			stack_cur.events = 0;
			fd->cb(fd, events);
			events = stack_cur.events & ULOOP_EVENT_MASK;
		} while (stack_cur.fd && events);
		fd_stack = stack_cur.next;

		return;
	}
}

int uloop_fd_add(struct uloop_fd *sock, unsigned int flags)
{
	unsigned int fl;
	int ret;

	if (!(flags & (ULOOP_READ | ULOOP_WRITE)))
		return uloop_fd_delete(sock);

	if (!sock->registered && !(flags & ULOOP_BLOCKING)) {
		fl = fcntl(sock->fd, F_GETFL, 0);
		fl |= O_NONBLOCK;
		fcntl(sock->fd, F_SETFL, fl);
	}

	ret = register_poll(sock, flags);
	if (ret < 0)
		goto out;

	if (uloop_fd_set_cb)
		uloop_fd_set_cb(sock, flags);

	sock->flags = flags;
	sock->registered = true;
	sock->eof = false;
	sock->error = false;

out:
	return ret;
}

int uloop_fd_delete(struct uloop_fd *fd)
{
	int ret;
	int i;

	for (i = 0; i < cur_nfds; i++) {
		if (cur_fds[cur_fd + i].fd != fd)
			continue;

		cur_fds[cur_fd + i].fd = NULL;
	}

	if (!fd->registered)
		return 0;

	if (uloop_fd_set_cb)
		uloop_fd_set_cb(fd, 0);

	fd->registered = false;
	uloop_fd_stack_event(fd, -1);
	ret = __uloop_fd_delete(fd);
	fd->flags = 0;

	return ret;
}

static int64_t tv_diff(struct timeval *t1, struct timeval *t2)
{
	return
		(t1->tv_sec - t2->tv_sec) * 1000 +
		(t1->tv_usec - t2->tv_usec) / 1000;
}

int uloop_timeout_add(struct uloop_timeout *timeout)
{
	struct uloop_timeout *tmp;
	struct list_head *h = &timeouts;

	if (timeout->pending)
		return -1;

	list_for_each_entry(tmp, &timeouts, list) {
		if (tv_diff(&tmp->time, &timeout->time) > 0) {
			h = &tmp->list;
			break;
		}
	}

	list_add_tail(&timeout->list, h);
	timeout->pending = true;

	return 0;
}

static void uloop_gettime(struct timeval *tv)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	tv->tv_sec = ts.tv_sec;
	tv->tv_usec = ts.tv_nsec / 1000;
}

int uloop_timeout_set(struct uloop_timeout *timeout, int msecs)
{
	struct timeval *time = &timeout->time;

	if (timeout->pending)
		uloop_timeout_cancel(timeout);

	uloop_gettime(time);

	time->tv_sec += msecs / 1000;
	time->tv_usec += (msecs % 1000) * 1000;

	if (time->tv_usec > 1000000) {
		time->tv_sec++;
		time->tv_usec -= 1000000;
	}

	return uloop_timeout_add(timeout);
}

int uloop_timeout_cancel(struct uloop_timeout *timeout)
{
	if (!timeout->pending)
		return -1;

	list_del(&timeout->list);
	timeout->pending = false;

	return 0;
}

int uloop_timeout_remaining(struct uloop_timeout *timeout)
{
	int64_t td;
	struct timeval now;

	if (!timeout->pending)
		return -1;

	uloop_gettime(&now);

	td = tv_diff(&timeout->time, &now);

	if (td > INT_MAX)
		return INT_MAX;
	else if (td < INT_MIN)
		return INT_MIN;
	else
		return (int)td;
}

int64_t uloop_timeout_remaining64(struct uloop_timeout *timeout)
{
	struct timeval now;

	if (!timeout->pending)
		return -1;

	uloop_gettime(&now);

	return tv_diff(&timeout->time, &now);
}

int uloop_process_add(struct uloop_process *p)
{
	struct uloop_process *tmp;
	struct list_head *h = &processes;

	if (p->pending)
		return -1;

	list_for_each_entry(tmp, &processes, list) {
		if (tmp->pid > p->pid) {
			h = &tmp->list;
			break;
		}
	}

	list_add_tail(&p->list, h);
	p->pending = true;

	return 0;
}

int uloop_process_delete(struct uloop_process *p)
{
	if (!p->pending)
		return -1;

	list_del(&p->list);
	p->pending = false;

	return 0;
}

static void uloop_handle_processes(void)
{
	struct uloop_process *p, *tmp;
	pid_t pid;
	int ret;

	do_sigchld = false;

	while (1) {
		pid = waitpid(-1, &ret, WNOHANG);
		if (pid < 0 && errno == EINTR)
			continue;

		if (pid <= 0)
			return;

		list_for_each_entry_safe(p, tmp, &processes, list) {
			if (p->pid < pid)
				continue;

			if (p->pid > pid)
				break;

			uloop_process_delete(p);
			p->cb(p, ret);
		}
	}

}

int uloop_interval_set(struct uloop_interval *timer, unsigned int msecs)
{
	return timer_register(timer, msecs);
}

int uloop_interval_cancel(struct uloop_interval *timer)
{
	return timer_remove(timer);
}

int64_t uloop_interval_remaining(struct uloop_interval *timer)
{
	return timer_next(timer);
}

static void uloop_signal_wake(int signo)
{
	uint8_t sigbyte = signo;

	if (signo == SIGCHLD)
		do_sigchld = true;

	do {
		if (write(waker_pipe, &sigbyte, 1) < 0) {
			if (errno == EINTR)
				continue;
		}
		break;
	} while (1);
}

static void uloop_handle_sigint(int signo)
{
	uloop_status = signo;
	uloop_cancelled = true;
	uloop_signal_wake(signo);
}

static void uloop_install_handler(int signum, void (*handler)(int), struct sigaction* old, bool add)
{
	struct sigaction s;
	struct sigaction *act;

	act = NULL;
	sigaction(signum, NULL, &s);

	if (add) {
		if (s.sa_handler == SIG_DFL) { /* Do not override existing custom signal handlers */
			memcpy(old, &s, sizeof(struct sigaction));
			s.sa_handler = handler;
			s.sa_flags = 0;
			act = &s;
		}
	}
	else if (s.sa_handler == handler) { /* Do not restore if someone modified our handler */
			act = old;
	}

	if (act != NULL)
		sigaction(signum, act, NULL);
}

static void uloop_ignore_signal(int signum, bool ignore)
{
	struct sigaction s;
	void *new_handler = NULL;

	sigaction(signum, NULL, &s);

	if (ignore) {
		if (s.sa_handler == SIG_DFL) /* Ignore only if there isn't any custom handler */
			new_handler = SIG_IGN;
	} else {
		if (s.sa_handler == SIG_IGN) /* Restore only if noone modified our SIG_IGN */
			new_handler = SIG_DFL;
	}

	if (new_handler) {
		s.sa_handler = new_handler;
		s.sa_flags = 0;
		sigaction(signum, &s, NULL);
	}
}

static void uloop_setup_signals(bool add)
{
	static struct sigaction old_sigint, old_sigchld, old_sigterm;

	uloop_install_handler(SIGINT, uloop_handle_sigint, &old_sigint, add);
	uloop_install_handler(SIGTERM, uloop_handle_sigint, &old_sigterm, add);

	if (uloop_handle_sigchld)
		uloop_install_handler(SIGCHLD, uloop_signal_wake, &old_sigchld, add);

	uloop_ignore_signal(SIGPIPE, add);
}

int uloop_signal_add(struct uloop_signal *s)
{
	struct list_head *h = &signals;
	struct uloop_signal *tmp;
	struct sigaction sa;

	if (s->pending)
		return -1;

	list_for_each_entry(tmp, &signals, list) {
		if (tmp->signo > s->signo) {
			h = &tmp->list;
			break;
		}
	}

	list_add_tail(&s->list, h);
	s->pending = true;

	sigaction(s->signo, NULL, &s->orig);

	if (s->orig.sa_handler != uloop_signal_wake) {
		sa.sa_handler = uloop_signal_wake;
		sa.sa_flags = 0;
		sigemptyset(&sa.sa_mask);
		sigaction(s->signo, &sa, NULL);
	}

	return 0;
}

int uloop_signal_delete(struct uloop_signal *s)
{
	if (!s->pending)
		return -1;

	list_del(&s->list);
	s->pending = false;

	if (s->orig.sa_handler != uloop_signal_wake)
		sigaction(s->signo, &s->orig, NULL);

	return 0;
}

static int64_t uloop_process_timeouts(struct timeval *tv)
{
	struct uloop_timeout *t;
	int64_t res;

	while (!list_empty(&timeouts)) {
		t = list_first_entry(&timeouts, struct uloop_timeout, list);

		res = tv_diff(&t->time, tv);
		if (res > 0)
			return res;

		uloop_timeout_cancel(t);
		if (t->cb)
			t->cb(t);
	}
	return -1;
}

static void uloop_clear_timeouts(void)
{
	struct uloop_timeout *t, *tmp;

	list_for_each_entry_safe(t, tmp, &timeouts, list)
		uloop_timeout_cancel(t);
}

static void uloop_clear_processes(void)
{
	struct uloop_process *p, *tmp;

	list_for_each_entry_safe(p, tmp, &processes, list)
		uloop_process_delete(p);
}

bool uloop_cancelling(void)
{
	return uloop_run_depth > 0 && uloop_cancelled;
}

static void handle_global_timeout(struct uloop_timeout *timeout) {
	global_current_uloop_timeout_reached = true;
}

int uloop_run_timeout(int timeout)
{
	int64_t next_time = 0;
	struct uloop_timeout uloop_global_timer;
	
	struct timeval tv;

	uloop_run_depth++;

	if (timeout >= 0) {
		uloop_global_timer.cb = &handle_global_timeout;
		uloop_timeout_set(&uloop_global_timer, timeout);
	}

	uloop_status = 0;
	global_current_uloop_timeout_reached = false;
	do {
		if (do_sigchld)
			uloop_handle_processes();

		if (uloop_cancelled)
			break;
		
		uloop_gettime(&tv);
		next_time = uloop_process_timeouts(&tv);

		if (uloop_cancelled)
			break;

		if (next_time >= 0) {
			uloop_run_events(next_time);
		}
	} while ((!uloop_cancelled) && (!global_current_uloop_timeout_reached));

	global_current_uloop_timeout_reached = false; // reset back for upper level of uloop
	--uloop_run_depth;

	return uloop_status;
}

void uloop_done(void)
{
	uloop_setup_signals(false);

	if (poll_fd >= 0) {
		close(poll_fd);
		poll_fd = -1;
	}

	if (waker_pipe >= 0) {
		uloop_fd_delete(&waker_fd);
		close(waker_pipe);
		close(waker_fd.fd);
		waker_pipe = -1;
	}

	uloop_clear_timeouts();
	uloop_clear_processes();
}
