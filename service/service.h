#ifndef SERVICE_SERVICE_H_
#define SERVICE_SERVICE_H_

#include <string>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "absl/types/any.h"
#include "service/service_state.h"

namespace hcoona {

class Service {
 public:
  Service() = default;
  virtual ~Service() = default;

  // Disallow copy
  Service(const Service&) = delete;
  Service& operator=(const Service&) = delete;

  // Allow move
  Service(Service&&) = default;
  Service& operator=(Service&&) = default;

  virtual absl::Status Init(absl::any data) = 0;
  virtual absl::Status Start() = 0;
  virtual absl::Status Stop() = 0;

  // TODO(zhangshuai.ustc): Add signal sink for state changing.

  virtual std::string name() const = 0;
  virtual ServiceState state() const = 0;
  virtual bool is_in_state(ServiceState state) const = 0;
  virtual absl::Time start_time() const = 0;

  virtual bool WaitForStateWithTimeout(ServiceState state,
                                       absl::Duration timeout) const = 0;
  virtual bool WaitForStateWithDeadline(ServiceState state,
                                        absl::Time deadline) const = 0;
};

}  // namespace hcoona

#endif  // SERVICE_SERVICE_H_
