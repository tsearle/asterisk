/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007-2008, Digium, Inc.
 *
 * Dwayne M. Hubbard <dhubbard@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*!
 * \file taskprocessor.h
 * \brief An API for managing task processing threads that can be shared across modules
 *
 * \author Dwayne M. Hubbard <dhubbard@digium.com>
 *
 * \note A taskprocessor is a named singleton containing a task queue that
 * serializes tasks pushed into it by [a] module(s) that reference the taskprocessor.
 * A taskprocessor is created the first time its name is requested via the
 * ast_taskprocessor_get() function or the ast_taskprocessor_create_with_listener()
 * function and destroyed when the taskprocessor reference count reaches zero. A
 * taskprocessor also contains an accompanying listener that is notified when changes
 * in the task queue occur.
 *
 * A task is a wrapper around a task-handling function pointer and a data
 * pointer.  A task is pushed into a taskprocessor queue using the
 * ast_taskprocessor_push(taskprocessor, taskhandler, taskdata) function and freed by the
 * taskprocessor after the task handling function returns.  A module releases its
 * reference to a taskprocessor using the ast_taskprocessor_unreference() function which
 * may result in the destruction of the taskprocessor if the taskprocessor's reference
 * count reaches zero. When the taskprocessor's reference count reaches zero, its
 * listener's shutdown() callback will be called. Any further attempts to execute tasks
 * will be denied.
 *
 * The taskprocessor listener has the flexibility of doling out tasks to best fit the
 * module's needs. For instance, a taskprocessor listener may have a single dispatch
 * thread that handles all tasks, or it may dispatch tasks to a thread pool.
 *
 * There is a default taskprocessor listener that will be used if a taskprocessor is
 * created without any explicit listener. This default listener runs tasks sequentially
 * in a single thread. The listener will execute tasks as long as there are tasks to be
 * processed. When the taskprocessor is shut down, the default listener will stop
 * processing tasks and join its execution thread.
 */

#ifndef __AST_TASKPROCESSOR_H__
#define __AST_TASKPROCESSOR_H__

struct ast_taskprocessor;

/*!
 * \brief ast_tps_options for specification of taskprocessor options
 *
 * Specify whether a taskprocessor should be created via ast_taskprocessor_get() if the taskprocessor
 * does not already exist.  The default behavior is to create a taskprocessor if it does not already exist
 * and provide its reference to the calling function.  To only return a reference to a taskprocessor if
 * and only if it exists, use the TPS_REF_IF_EXISTS option in ast_taskprocessor_get().
 */
enum ast_tps_options {
	/*! \brief return a reference to a taskprocessor, create one if it does not exist */
	TPS_REF_DEFAULT = 0,
	/*! \brief return a reference to a taskprocessor ONLY if it already exists */
	TPS_REF_IF_EXISTS = (1 << 0),
};

struct ast_taskprocessor_listener;

struct ast_taskprocessor_listener_callbacks {
	/*!
	 * \brief Allocate the listener's private data
	 *
	 * It is not necessary to assign the private data to the listener.
	 *
	 * \param listener The listener to which the private data belongs
	 * \retval NULL Error while attempting to initialize private data
	 * \retval non-NULL Allocated private data
	 */
	void *(*alloc)(struct ast_taskprocessor_listener *listener);
	/*!
	 * \brief Indicates a task was pushed to the processor
	 *
	 * \param listener The listener
	 * \param was_empty If non-zero, the taskprocessor was empty prior to the task being pushed
	 */
	void (*task_pushed)(struct ast_taskprocessor_listener *listener, int was_empty);
	/*!
	 * \brief Indicates the task processor has become empty
	 *
	 * \param listener The listener
	 */
	void (*emptied)(struct ast_taskprocessor_listener *listener);
	/*!
	 * \brief Indicates the taskprocessor wishes to die.
	 *
	 * All operations on the task processor must to be stopped in
	 * this callback.
	 *
	 * After this callback returns, it is NOT safe to operate on the
	 * listener's reference to the taskprocessor.
	 *
	 * \param listener The listener
	 */
	void (*shutdown)(struct ast_taskprocessor_listener *listener);
	/*!
	 * \brief Destroy the listener's private data
	 *
	 * It is required that you free the private data in this callback
	 * in addition to the private data's individual fields.
	 *
	 * \param private_data The listener's private data
	 */
	void (*destroy)(void *private_data);
};

/*!
 * \brief A listener for taskprocessors
 *
 * When a taskprocessor's state changes, the listener
 * is notified of the change. This allows for tasks
 * to be addressed in whatever way is appropriate for
 * the module using the taskprocessor.
 */
struct ast_taskprocessor_listener {
	/*! The callbacks the taskprocessor calls into to notify of state changes */
	const struct ast_taskprocessor_listener_callbacks *callbacks;
	/*! The taskprocessor that the listener is listening to */
	struct ast_taskprocessor *tps;
	/*! Data private to the listener */
	void *private_data;
};

/*!
 * Allocate a taskprocessor listener
 *
 * This will result in the listener being allocated with the specified
 * callbacks. The listener's alloc() callback will be called to allocate
 * private data for the listener. The private data will be assigned to the
 * listener when the listener's alloc() function returns.
 *
 * \param callbacks The callbacks to assign to the listener
 * \retval NULL Failure
 * \retval non-NULL The newly allocated taskprocessor listener
 */
struct ast_taskprocessor_listener *ast_taskprocessor_listener_alloc(const struct ast_taskprocessor_listener_callbacks *callbacks);

/*!
 * \brief Get a reference to a taskprocessor with the specified name and create the taskprocessor if necessary
 *
 * The default behavior of instantiating a taskprocessor if one does not already exist can be
 * disabled by specifying the TPS_REF_IF_EXISTS ast_tps_options as the second argument to ast_taskprocessor_get().
 * \param name The name of the taskprocessor
 * \param create Use 0 by default or specify TPS_REF_IF_EXISTS to return NULL if the taskprocessor does
 * not already exist
 * return A pointer to a reference counted taskprocessor under normal conditions, or NULL if the
 * TPS_REF_IF_EXISTS reference type is specified and the taskprocessor does not exist
 * \since 1.6.1
 */
struct ast_taskprocessor *ast_taskprocessor_get(const char *name, enum ast_tps_options create);

/*!
 * \brief Create a taskprocessor with a custom listener
 *
 * \param name The name of the taskprocessor to create
 * \param listener The listener for operations on this taskprocessor
 * \retval NULL Failure
 * \reval non-NULL success
 */
struct ast_taskprocessor *ast_taskprocessor_create_with_listener(const char *name, struct ast_taskprocessor_listener *listener);

/*!
 * \brief Unreference the specified taskprocessor and its reference count will decrement.
 *
 * Taskprocessors use astobj2 and will unlink from the taskprocessor singleton container and destroy
 * themself when the taskprocessor reference count reaches zero.
 * \param tps taskprocessor to unreference
 * \return NULL
 * \since 1.6.1
 */
void *ast_taskprocessor_unreference(struct ast_taskprocessor *tps);

/*!
 * \brief Push a task into the specified taskprocessor queue and signal the taskprocessor thread
 * \param tps The taskprocessor structure
 * \param task_exe The task handling function to push into the taskprocessor queue
 * \param datap The data to be used by the task handling function
 * \retval 0 success
 * \retval -1 failure
 * \since 1.6.1
 */
int ast_taskprocessor_push(struct ast_taskprocessor *tps, int (*task_exe)(void *datap), void *datap);

/*!
 * \brief Pop a task off the taskprocessor and execute it.
 * \param tps The taskprocessor from which to execute.
 * \retval 0 There is no further work to be done.
 * \retval 1 Tasks still remain in the taskprocessor queue.
 */
int ast_taskprocessor_execute(struct ast_taskprocessor *tps);

/*!
 * \brief Return the name of the taskprocessor singleton
 * \since 1.6.1
 */
const char *ast_taskprocessor_name(struct ast_taskprocessor *tps);

#endif /* __AST_TASKPROCESSOR_H__ */
