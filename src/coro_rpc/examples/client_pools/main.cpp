/*
 * Copyright (c) 2022, Alibaba Group Holding Limited;
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <thread>

#include "asio/io_context.hpp"
#include "asio/steady_timer.hpp"
#include "async_simple/coro/Lazy.h"
#include "async_simple/coro/Sleep.h"
#include "coro_io/client_pool.hpp"
#include "coro_io/coro_io.hpp"
#include "coro_rpc/coro_rpc_client.hpp"
std::string echo(std::string_view sv);
using namespace coro_rpc;
using namespace async_simple::coro;
using namespace std::string_view_literals;
using namespace std::chrono_literals;
std::atomic<uint64_t> qps = 0;

std::atomic<uint64_t> working_echo = 0;
/*!
 * \example helloworld/concurrency_clients.main.cpp
 * \brief demo for run concurrency clients
 */

Lazy<void> call_echo(coro_io::client_pools<coro_rpc_client> &client_pools,
                     int cnt) {
  ++working_echo;
  for (int i = 0; i < cnt; ++i) {
    auto res = co_await client_pools.send_request(
        i % 2 ? "localhost:8801" : "127.0.0.1:8801",
        [=](coro_rpc_client &client) -> Lazy<void> {
          auto res = co_await client.call<echo>("Hello world!");
          if (!res.has_value()) {
            std::cout << "coro_rpc err: \n";
            co_return;
          }
          if (res.value() != "Hello world!"sv) {
            std::cout << "err echo resp: \n";
            co_return;
          }
          ++qps;
          co_return;
        });
    if (!res) {
      std::cout << "client pool err: connect failed.\n";
    }
  }
  --working_echo;
}

Lazy<void> qps_watcher(coro_io::client_pools<coro_rpc_client> &clients) {
  using namespace std::chrono_literals;
  while (working_echo > 0) {
    co_await coro_io::sleep_for(1s);
    uint64_t cnt = qps.exchange(0);
    std::cout << "QPS:" << cnt << " working echo:" << working_echo << std::endl;
    std::cout << "free client for localhost: "
              << (clients["localhost:8801"])->free_client_count() << std::endl;
    std::cout << "free client for 127.0.0.1: "
              << (clients["127.0.0.1:8801"])->free_client_count() << std::endl;
    cnt = 0;
  }
}

int main() {
  auto thread_cnt = std::thread::hardware_concurrency();
  coro_io::client_pools<coro_rpc_client> clients{
      coro_io::client_pool<coro_rpc_client>::pool_config{
          .max_connection_ = thread_cnt * 20, .idle_timeout_ = 5s}};
  for (int i = 0, lim = thread_cnt * 20; i < lim; ++i) {
    call_echo(clients, 10000).start([](auto &&) {
    });
  }
  syncAwait(qps_watcher(clients));
  std::cout << "Done!" << std::endl;
  return 0;
}