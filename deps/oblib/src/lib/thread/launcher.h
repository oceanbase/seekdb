/*
 * Copyright (c) 2025 OceanBase.
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

namespace oceanbase {
namespace lib {

////
// How to use Launcher?
//
// if (OB_SUCC(launcher.init())) {
//   if (OB_SUCC(launcher.start())) {
//     wait_stop_signal();
//     launcher.stop();
//     launcher.wait();
//   }
//   launcher.destroy();
// }
//
////
// How to combine launchers?
//
// int init()
// {
//   if (OB_SUCC(launcher1.init())) {
//     if (OB_SUCC(launcher2.init())) {
//       if (OB_SUCC(launcher3.init())) {
//       } else {
//         launcher1.destroy();
//         launcher2.destroy();
//       }
//     } else {
//       launcher1.destroy();
//     }
//   }
// }
// int start()
// {
//   if (OB_SUCC(launcher1.start())) {
//     if (OB_SUCC(launcher2.start())) {
//       if (OB_SUCC(launcher3.start())) {
//       } else {
//         launcher1.stop();
//         launcher2.stop();
//       }
//     } else {
//       launcher1.stop();
//     }
//   }
// }
////
// Overall principle
//
// 1. Launcher state transition rules:
//
//     init/destroy        start     stop
// Uninitialized <===> Initialization not started ==> Started ==> Stopping
//                  /\                     ||
//                  ||         wait        ||
//                  \=======================/
//
// 2. init and destroy appear in pairs, start and stop appear in pairs.
// 3. do not start the thread in init, nor stop the thread in destroy, this will disrupt the outer logic.
// 4. Implement init, if it fails midway, need to call the destroy function of already successfully initialized members,
//    Do not have extra states. start function similarly.
// 5. Error state transition, for example, calling the start method when "uninitialized", belongs to a bug that needs to be fixed.
//    Recommend printing ERROR logs to expose issues early.
// 6. All self-contained thread classes should inherit and follow this set of rules.
//
class ILauncher {
public:
  virtual ~ILauncher() {}
  // Initialize resources
  virtual int init() = 0;
  // Start thread
  virtual int start() = 0;
  // Stop thread
  //
  // Note that the interface return does not guarantee that the thread has already stopped, only when wait returns does it indicate that the thread has exited.
  virtual void stop() = 0;
  // Use in conjunction with the stop interface, wait returns indicating that all threads have successfully exited.
  virtual void wait() = 0;
  // Release resources.
  virtual void destroy() = 0;
};


}  // lib
}  // oceanbase
