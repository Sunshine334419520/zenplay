#pragma once

namespace loki {

// 这个ID代表着你将要创建什么类型的线程，并且会对应的创建对应的message_loop.
enum ID { UI = 0, IO, NET, DB, ID_COUNT = 12 };

}   // namespace loki
