/*
 * Copyright (C) 2022 Open Source Robotics Foundation
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
 *
 */

#include <ignition/msgs/boolean.pb.h>
#include <ignition/msgs/stringmsg_v.pb.h>

#include <atomic>
#include <chrono>
#include <string>

#include <ignition/common/Profiler.hh>
#include <ignition/plugin/Register.hh>
#include <ignition/transport/Node.hh>
#include <sdf/sdf.hh>
#include "ignition/gazebo/Model.hh"
#include "ignition/gazebo/Util.hh"
#include "CommsEndpoint.hh"

using namespace ignition;
using namespace gazebo;
using namespace systems;

class ignition::gazebo::systems::CommsEndpoint::Implementation
{
  /// \brief Send the bind request.
  public: void Bind();

  /// \brief Service response callback.
  /// \brief \param[in] _rep Unused.
  /// \brief \param[in] _result Bind result.
  public: void BindCallback(const ignition::msgs::Boolean &_rep,
                            const bool _result);

  /// \brief The address.
  public: std::string address;

  /// \brief The topic where the messages will be delivered.
  public: std::string topic;

  /// \brief Model interface
  public: Model model{kNullEntity};

  /// \brief True when the address has been bound in the broker.
  public: std::atomic_bool bound{false};

  /// \brief Service where the broker is listening bind requests.
  public: std::string bindSrv = "/broker/bind";

  /// \brief Service where the broker is listening unbind requests.
  public: std::string unbindSrv = "/broker/unbind";

  /// \brief Message to send the bind request.
  public: ignition::msgs::StringMsg_V bindReq;

  /// \brief Message to send the unbind request.
  public: ignition::msgs::StringMsg_V unbindReq;

  /// \brief Time between bind retries (secs).
  public: std::chrono::steady_clock::duration bindRequestPeriod{1};

  /// \brief Last simulation time we tried to bind.
  public: std::chrono::steady_clock::duration lastBindRequestTime{-2};

  /// \brief The ignition transport node.
  public: std::unique_ptr<ignition::transport::Node> node;
};

//////////////////////////////////////////////////
void CommsEndpoint::Implementation::BindCallback(
  const ignition::msgs::Boolean &/*_rep*/, const bool _result)
{
  if (_result)
    this->bound = true;

  igndbg << "Succesfuly bound to [" << this->address << "] on topic ["
         << this->topic << "]" << std::endl;
}

//////////////////////////////////////////////////
void CommsEndpoint::Implementation::Bind()
{
  this->node->Request(this->bindSrv, this->bindReq,
    &CommsEndpoint::Implementation::BindCallback, this);
}

//////////////////////////////////////////////////
CommsEndpoint::CommsEndpoint()
  : dataPtr(ignition::utils::MakeUniqueImpl<Implementation>())
{
  this->dataPtr->node = std::make_unique<ignition::transport::Node>();
}

//////////////////////////////////////////////////
CommsEndpoint::~CommsEndpoint()
{
  if (!this->dataPtr->bound)
    return;

  // Unbind.
  // We use a oneway request because we're not going
  // to be alive to check the result or retry.
  this->dataPtr->node->Request(
    this->dataPtr->unbindSrv, this->dataPtr->unbindReq);
}

//////////////////////////////////////////////////
void CommsEndpoint::Configure(const Entity &_entity,
    const std::shared_ptr<const sdf::Element> &_sdf,
    EntityComponentManager &_ecm,
    EventManager &/*_eventMgr*/)
{
  // Parse <address>.
  if (!_sdf->HasElement("address"))
  {
    ignerr << "No <address> specified." << std::endl;
    return;
  }
  this->dataPtr->address = _sdf->Get<std::string>("address");

  // Parse <topic>.
  if (!_sdf->HasElement("topic"))
  {
    ignerr << "No <topic> specified." << std::endl;
    return;
  }
  this->dataPtr->topic = _sdf->Get<std::string>("topic");

  // Parse <broker>.
  if (_sdf->HasElement("broker"))
  {
    sdf::ElementPtr elem = _sdf->Clone()->GetElement("broker");
    this->dataPtr->bindSrv =
      elem->Get<std::string>("bind_service", this->dataPtr->bindSrv).first;
    this->dataPtr->unbindSrv =
      elem->Get<std::string>("unbind_service", this->dataPtr->unbindSrv).first;
  }

  // Set model.
  this->dataPtr->model = Model(_entity);

  // Prepare the bind parameters.
  this->dataPtr->bindReq.add_data(this->dataPtr->address);
  this->dataPtr->bindReq.add_data(this->dataPtr->model.Name(_ecm));
  this->dataPtr->bindReq.add_data(this->dataPtr->topic);

  // Prepare the unbind parameters.
  this->dataPtr->unbindReq.add_data(this->dataPtr->address);
  this->dataPtr->unbindReq.add_data(this->dataPtr->topic);
}

//////////////////////////////////////////////////
void CommsEndpoint::PreUpdate(
    const ignition::gazebo::UpdateInfo &_info,
    ignition::gazebo::EntityComponentManager &/*_ecm*/)
{
  IGN_PROFILE("CommsEndpoint::PreUpdate");

  if (this->dataPtr->bound)
    return;

  auto elapsed = _info.simTime - this->dataPtr->lastBindRequestTime;
  if (elapsed > std::chrono::steady_clock::duration::zero() &&
      elapsed < this->dataPtr->bindRequestPeriod)
  {
    return;
  }
  this->dataPtr->lastBindRequestTime = _info.simTime;

  // Let's try to bind.
  this->dataPtr->Bind();
}

IGNITION_ADD_PLUGIN(CommsEndpoint,
                    ignition::gazebo::System,
                    CommsEndpoint::ISystemConfigure,
                    CommsEndpoint::ISystemPreUpdate)

IGNITION_ADD_PLUGIN_ALIAS(CommsEndpoint,
                          "ignition::gazebo::systems::CommsEndpoint")
