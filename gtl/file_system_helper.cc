/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "gtl/file_system_helper.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "gtl/file_system.h"
#include "gtl/path.h"

namespace gtl {
namespace internal {

namespace {

constexpr int kNumThreads = 8;

// Run a function in parallel using a ThreadPool, but skip the ThreadPool
// on the iOS platform due to its problems with more than a few threads.
void ForEach(int first, int last, const std::function<void(int)>& f) {
#if 1
  static_cast<void>(kNumThreads);  // Eliminate warning.
  for (int i = first; i < last; i++) {
    f(i);
  }
#else
  int num_threads = std::min(kNumThreads, last - first);
  thread::ThreadPool threads(Env::Default(), "ForEach", num_threads);
  for (int i = first; i < last; i++) {
    threads.Schedule([f, i] { f(i); });
  }
#endif
}

}  // namespace

absl::Status GetMatchingPaths(FileSystem* fs, const std::string& pattern,
                              std::vector<std::string>* results) {
  results->clear();
  // Find the fixed prefix by looking for the first wildcard.
  std::string fixed_prefix = pattern.substr(0, pattern.find_first_of("*?[\\"));
  std::string eval_pattern = pattern;
  std::vector<std::string> all_files;
  std::string dir(Dirname(fixed_prefix));
  // If dir is empty then we need to fix up fixed_prefix and eval_pattern to
  // include . as the top level directory.
  if (dir.empty()) {
    dir = ".";
    fixed_prefix = JoinPath(dir, fixed_prefix);
    eval_pattern = JoinPath(dir, pattern);
  }

  // Setup a BFS to explore everything under dir.
  std::deque<std::string> dir_q;
  dir_q.push_back(dir);
  absl::Status ret;  // Status to return.
  // children_dir_status holds is_dir status for children. It can have three
  // possible values: OK for true; FAILED_PRECONDITION for false; CANCELLED
  // if we don't calculate IsDirectory (we might do that because there isn't
  // any point in exploring that child path).
  std::vector<absl::Status> children_dir_status;
  while (!dir_q.empty()) {
    std::string current_dir = dir_q.front();
    dir_q.pop_front();
    std::vector<std::string> children;
    absl::Status s = fs->GetChildren(current_dir, &children);
    // In case PERMISSION_DENIED is encountered, we bail here.
    if (absl::IsPermissionDenied(s)) {
      continue;
    }
    ret.Update(s);
    if (children.empty()) continue;
    // This IsDirectory call can be expensive for some FS. Parallelizing it.
    children_dir_status.resize(children.size());
    ForEach(0, children.size(),
            [fs, &current_dir, &children, &fixed_prefix,
             &children_dir_status](int i) {
              const std::string child_path = JoinPath(current_dir, children[i]);
              // In case the child_path doesn't start with the fixed_prefix then
              // we don't need to explore this path.
              if (!absl::StartsWith(child_path, fixed_prefix)) {
                children_dir_status[i] =
                    absl::CancelledError("Operation not needed");
              } else {
                children_dir_status[i] = fs->IsDirectory(child_path);
              }
            });
    for (int i = 0; i < children.size(); ++i) {
      const std::string child_path = JoinPath(current_dir, children[i]);
      // If the IsDirectory call was cancelled we bail.
      if (absl::IsCancelled(children_dir_status[i])) {
        continue;
      }
      // If the child is a directory add it to the queue.
      if (children_dir_status[i].ok()) {
        dir_q.push_back(child_path);
      }
      all_files.push_back(child_path);
    }
  }

  // Match all obtained files to the input pattern.
  for (const auto& f : all_files) {
    if (fs->Match(f, eval_pattern)) {
      results->push_back(f);
    }
  }
  return ret;
}

}  // namespace internal
}  // namespace gtl
