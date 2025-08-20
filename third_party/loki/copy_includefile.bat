mkdir build
mkdir build\include
mkdir build\include\loki
mkdir build\include\loki\threading

copy "src\loki_export.h" build\include\loki\loki_export.h
copy "src\bind_util.h" build\include\loki\bind_util.h
copy "src\callback.h" build\include\loki\callback.h
copy "src\hash.h" build\include\loki\hash.h
copy "src\location.h" build\include\loki\location.h
copy "src\single_thread_task_runner.h" build\include\loki\single_thread_task_runner.h
copy "src\sequenced_task_runner_helpers.h" build\include\loki\sequenced_task_runner_helpers.h
copy "src\sequenced_task_runner.h" build\include\loki\sequenced_task_runner.h
copy "src\macor.h" build\include\loki\macor.h
copy "src\main_message_loop.h" build\include\loki\main_message_loop.h
copy "src\main_message_loop_std.h" build\include\loki\main_message_loop_std.h
copy "src\main_message_loop_with_not_main_thread.h" build\include\loki\main_message_loop_with_not_main_thread.h
copy "src\post_task_and_reply_with_result_internal.h" build\include\loki\post_task_and_reply_with_result_internal.h
copy "src\post_task_interface.h" build\include\loki\post_task_interface.h
copy "src\task_runner.h" build\include\loki\task_runner.h
copy "src\task_runner_util.h" build\include\loki\task_runner_util.h
copy "src\threading\thread_define.h" build\include\loki\threading\thread_define.h

