/**
* @Author: YangGuang
* @Date:   2018-11-16
* @Email:  guang334419520@126.com
* @Filename: browser_thread.h
* @Last modified by:  YangGuang
*/
#include "threading/loki_thread_impl.h"

#include <atomic>

#include "lazy_instance.h"
#include "logging.h"
#include "task_runner_util.h"
#include "ptr_util.h"

namespace loki {

// 一个SingleThreadTaskRunner的实例化，专门针对于LokiThread.
class BrowserThreadTaskRunner : public loki::SingleThreadTaskRunner {
 public:
	explicit BrowserThreadTaskRunner(ID identifier)
		: id_(identifier) {}

	bool PostDelayedTask(const loki::Location& from_here,
						 loki::OnceClosure task,
						 std::chrono::milliseconds delay) OVERRIDE {
		return LokiThread::PostDelayedTask(id_, from_here, std::move(task),
											  delay);
	}

	bool PostNonNestableDelayedTask(const loki::Location& from_here,
								   loki::OnceClosure task,
								   std::chrono::milliseconds delay) OVERRIDE {
		return LokiThread::PostNonNestableDelayedTask(id_, from_here,
														 std::move(task),
														 delay);
		
	}

	bool RunsTasksInCurrentSequence() OVERRIDE {
		return LokiThread::CurrentlyOn(id_);
	}

 protected:
	 template <class T>
	 friend struct loki::DefaultDestroyTraits;
	 ~BrowserThreadTaskRunner() OVERRIDE {}
 private:
	 ID id_;
	 DISALLOW_COPY_AND_ASSIGN(BrowserThreadTaskRunner);
};

// 这个结构体是一个帮助BrowserThreadTaskRunner初始化的.
struct BrowserThreadTaskRunners {
	BrowserThreadTaskRunners() {
		for (int i = 0; i < ID_COUNT; ++i) {
			proxies[i] = 
				std::move(loki::WrapShared(new BrowserThreadTaskRunner(
					static_cast<ID>(i))));
		}
	}

	std::shared_ptr<loki::SingleThreadTaskRunner> proxies[ID_COUNT];
};

loki::LazyInstance<BrowserThreadTaskRunners>::Leaky g_task_runners;
	//LAZY_INSTANCE_INITIALIZER;

// 一个browser thread状态为了LokiThread::ID.
enum BrowserThreadState {
	// LokiThread::ID 还没有和任何的东西关联.
	UNINITIALIZED = 0,
	// LokiThread::ID 已经和TaskRunner进行了关联，并且是又接受到任务
	RUNNING,
	// LokiThread::ID 已经不会接受任务的tasks(它仍然还是和TaksRunner关联).
	SHTUDOWN
};

struct BrowserThreadGlobals {
	BrowserThreadGlobals() {}

	std::shared_ptr<loki::SingleThreadTaskRunner>
		task_runners[ID_COUNT];

	std::atomic<BrowserThreadState> states[ID_COUNT] = {};
};

loki::LazyInstance<BrowserThreadGlobals>::Leaky
	g_globals; //= LAZY_INSTANCE_INITIALIZER;

bool PostTaskHelper(ID identifier,
					const loki::Location& from_here,
					loki::OnceClosure task,
					std::chrono::milliseconds delay,
					bool nestable) {
	DCHECK_GE(identifier, 0);
	DCHECK_LT(identifier, ID_COUNT);

	BrowserThreadGlobals& globals = g_globals.Get();

	DCHECK_GE(globals.states[identifier].load(),
			  BrowserThreadState::RUNNING);
	DCHECK(globals.task_runners[identifier]);

	if (nestable) {
		return globals.task_runners[identifier]->PostDelayedTask(
			from_here, std::move(task), delay);
	}
	else {
		return globals.task_runners[identifier]->PostNonNestableDelayedTask(
			from_here, std::move(task), delay);
	}
}

LokiThreadImpl::LokiThreadImpl(
	ID identifier,
	std::shared_ptr<loki::SingleThreadTaskRunner> task_runner)
	: identifier_(identifier) {
	DCHECK_GE(identifier_, 0);
	DCHECK_LT(identifier_, ID_COUNT);
	DCHECK(task_runner);

	BrowserThreadGlobals& globals = g_globals.Get();

	DCHECK_EQ(globals.states[identifier_].load(std::memory_order_acquire),
			  BrowserThreadState::UNINITIALIZED);
	globals.states[identifier_].store(BrowserThreadState::RUNNING,
									  std::memory_order_seq_cst);

	DCHECK(!globals.task_runners[identifier_]);
	globals.task_runners[identifier] = std::move(task_runner);
}


LokiThreadImpl::~LokiThreadImpl() {
	BrowserThreadGlobals& globals = g_globals.Get();

	DCHECK_EQ(globals.states[identifier_].load(std::memory_order_acquire), 
			  BrowserThreadState::RUNNING);
	globals.states[identifier_].store(BrowserThreadState::SHTUDOWN,
									  std::memory_order_seq_cst);

	DCHECK(globals.task_runners[identifier_]);
}

// static.
bool LokiThread::IsThreadinitialized(ID identifier) {
	DCHECK_GE(identifier, 0);
	DCHECK_LT(identifier, ID_COUNT);

	BrowserThreadGlobals& globals = g_globals.Get();
	return globals.states[identifier].load(std::memory_order_acquire) ==
		   BrowserThreadState::RUNNING;
}

// static method.
bool LokiThread::CurrentlyOn(ID identifier) {
	DCHECK_GE(identifier, 0);
	DCHECK_LT(identifier, ID_COUNT);

	BrowserThreadGlobals& globals = g_globals.Get();
	return globals.task_runners[identifier] &&
		   globals.task_runners[identifier]->RunsTasksInCurrentSequence();
}

bool LokiThread::GetCurrentThreadIdentifier(ID* identifier) {
	BrowserThreadGlobals& globals = g_globals.Get();

	for (int i = 0; i < ID_COUNT; ++i) {
		if (globals.task_runners[i] &&
			globals.task_runners[i]->RunsTasksInCurrentSequence()) {
			*identifier = static_cast<ID>(i);
			return true;
		}
	}
	return false;
}

// static method.
bool LokiThread::PostTask(ID identifier,
							 const loki::Location& from_here,
							 loki::OnceClosure task) {
	return PostTaskHelper(identifier, from_here, std::move(task),
						  std::chrono::milliseconds(0), true);
}

// static method.
bool LokiThread::PostDelayedTask(ID identifier,
									const loki::Location & from_here,
									loki::OnceClosure task,
									std::chrono::milliseconds delay) {
	return PostTaskHelper(identifier, from_here, std::move(task),
						  delay, true);
}

// static method.
bool LokiThread::PostNonNestableTask(ID identifier,
										const loki::Location & from_here,
										loki::OnceClosure task) {
	return PostTaskHelper(identifier, from_here, std::move(task),
						  std::chrono::milliseconds(0), false);
}

// static method.
bool LokiThread::PostNonNestableDelayedTask(ID identifier,
											   const loki::Location & from_here,
											   loki::OnceClosure task,
											   std::chrono::milliseconds delay) {
	return PostTaskHelper(identifier, from_here, std::move(task),
						  delay, false);
}

// static method.
bool LokiThread::PostTaskAndReply(ID identifier,
									 const loki::Location & from_here,
									 loki::OnceClosure task,
									 loki::OnceClosure reply) {
	return GetTaskRunnerForThread(identifier)
		->PostTaskAndReplay(from_here, std::move(task), std::move(reply));
}

// static method.
void LokiThread::PostAfterStartupTask(const loki::Location & from_here,
										 const std::shared_ptr<loki::TaskRunner>& task_runner,
										 loki::OnceClosure task) {
}

// static method.
std::shared_ptr<loki::SingleThreadTaskRunner> 
LokiThread::GetTaskRunnerForThread(ID identifier) {
	return g_task_runners.Get().proxies[identifier];
}

}	// namespace loki.