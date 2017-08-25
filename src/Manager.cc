/*
 * Copyright (C) 2017 Open Source Robotics Foundation
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

#include <atomic>

#include <ignition/common/PluginLoader.hh>
#include <ignition/common/SystemPaths.hh>
#include <ignition/sensors/Manager.hh>


using namespace ignition::sensors;


struct PluginDescription
{
  /// \brief Name of a plugin to load
  public: std::string name;

  /// \brief Library name of a plugin to load
  public: std::string fileName;

  /// \brief element pointer for plugin config data
  public: sdf::ElementPtr element;
};


class ignition::sensors::ManagerPrivate
{
  /// \brief constructor
  public: ManagerPrivate();

  /// \brief destructor
  public: ~ManagerPrivate();

  /// \brief determine which plugins are needed based on an SDF element
  /// \return the number of plugins needed
  public: std::vector<PluginDescription> DetermineRequiredPlugins(
              sdf::ElementPtr _sdf);

  /// \brief load a plugin and return a shared_ptr
  public: std::shared_ptr<Sensor> LoadPlugin(const PluginDescription &_desc);

  // TODO use a map so sensors can be removed without changing their id
  /// \brief loaded sensors (index + 1 is sensor id)
  public: std::vector<std::shared_ptr<Sensor>> sensors;

  /// \brief Ignition Rendering manager
  public: ignition::rendering::Manager *renderingManager;

  /// \brief Instance used to find stuff on the file system
  public: ignition::common::SystemPaths systemPaths;

  /// \brief Instance used to load plugins
  public: ignition::common::PluginLoader pl;
};


//////////////////////////////////////////////////
ManagerPrivate::ManagerPrivate()
{
}

//////////////////////////////////////////////////
ManagerPrivate::~ManagerPrivate()
{
}

//////////////////////////////////////////////////
std::shared_ptr<Sensor> ManagerPrivate::LoadPlugin(
    const PluginDescription &_desc)
{
  auto fullPath = this->systemPaths.FindSharedLibrary(_desc.fileName);
  if (fullPath.size() == 0)
    return false;

  auto pluginName = pl.LoadLibrary(fullPath);
  if (pluginName.size() == 0)
    return false;

  auto instance = pl.Instantiate<Sensor>(pluginName);
  if (!instance)
    return false;

  // Shared pointer so others can access plugins
  std::shared_ptr<Sensor> sharedInst = std::move(instance);
  return sharedInst;
}

//////////////////////////////////////////////////
std::vector<PluginDescription> ManagerPrivate::DetermineRequiredPlugins(
    sdf::ElementPtr _sdf)
{
  std::vector<PluginDescription> pluginDescriptions;
  // Get info about plugins
  if (_sdf->HasElement("plugin"))
  {
    sdf::ElementPtr pluginElem = _sdf->GetElement("plugin");
    while (pluginElem)
    {
      PluginDescription desc;
      desc.name = pluginElem->Get<std::string>("name");
      desc.fileName = pluginElem->Get<std::string>("filename");
      desc.element = pluginElem;
      pluginDescriptions.push_back(desc);
      pluginElem = pluginElem->GetNextElement("plugin");
    }
  }

  // Load built-in plugins for sensors which are defined by SDFormat
  // ONLY IF there is no plugin already defined
  if (pluginDescriptions.empty())
  {
    std::vector<std::pair<std::string, std::string>> builtinPlugins = {
      {"camera", "ignition-sensors-camera"},
      {"altimeter", "ignition-sensors-altimeter"},
      {"contact", "ignition-sensors-contact"},
      {"gps", "ignition-sensors-gps"},
      {"imu", "ignition-sensors-imu"},
      {"logical_camera", "ignition-sensors-logical-camera"},
      {"magnetometer", "ignition-sensors-magnetometer"},
      {"ray", "ignition-sensors-ray"},
      {"sonar", "ignition-sensors-sonar"},
      {"transceiver", "ignition-sensors-transceiver"},
      {"force_torque", "ignition-sensors-force_torque"},
    };

    for (auto builtin : builtinPlugins)
    {
      if (_sdf->HasElement(builtin.first))
      {
        sdf::ElementPtr pluginElem = _sdf->GetElement(builtin.first);
        PluginDescription desc;
        desc.name = "__builtin__";
        desc.fileName = builtin.second;
        desc.element = _sdf;
        pluginDescriptions.push_back(desc);
      }
    }
  }
  return pluginDescriptions;
}

//////////////////////////////////////////////////
Manager::Manager() :
  dataPtr(new ManagerPrivate)
{
}

//////////////////////////////////////////////////
Manager::~Manager()
{
}

//////////////////////////////////////////////////
bool Manager::Init()
{
  this->dataPtr = std::make_unique<ManagerPrivate>();
  return true;
}

//////////////////////////////////////////////////
bool Manager::Init(ignition::rendering::Manager &_rendering)
{
  bool success = this->Init();
  if (success)
  {
    this->SetRendering(_rendering);
  }
  return success;
}

//////////////////////////////////////////////////
void Manager::SetRendering(ignition::rendering::Manager &_rendering)
{
  this->dataPtr->renderingManager = &_rendering;
}

//////////////////////////////////////////////////
ignition::rendering::Manager &Manager::RenderingManager() const
{
  return *(this->dataPtr->renderingManager);
}

//////////////////////////////////////////////////
std::shared_ptr<ignition::sensors::Sensor> Manager::Sensor(
          ignition::sensors::SensorId _id)
{
  if (_id <= 0 || _id > this->dataPtr->sensors.size())
    return std::shared_ptr<ignition::sensors::Sensor>();
  return this->dataPtr->sensors.at(_id - 1);
}

//////////////////////////////////////////////////
std::vector<ignition::sensors::SensorId> Manager::LoadSensor(
    sdf::ElementPtr &_sdf)
{
  std::vector<ignition::sensors::SensorId> sensorIds;
  auto pluginDescriptions = this->dataPtr->DetermineRequiredPlugins(_sdf);
  for (auto const &desc : pluginDescriptions)
  {
    auto sharedInst = this->dataPtr->LoadPlugin(desc);
    if (!sharedInst)
      continue;

    ignition::sensors::SensorId id = this->dataPtr->sensors.size() + 1;
    sharedInst->Init(this, id);
    if (sharedInst->Load(desc.element))
    {
      this->dataPtr->sensors.push_back(sharedInst);
      sensorIds.push_back(id);
    }
  }
  return sensorIds;
}

//////////////////////////////////////////////////
void Manager::AddPluginPaths(const std::string &_paths)
{
  this->dataPtr->systemPaths.AddPluginPaths(_paths);
}

//////////////////////////////////////////////////
void Manager::Remove(const ignition::sensors::SensorId _id)
{
  // TODO remove sensor
}

//////////////////////////////////////////////////
void Manager::RunOnce(const ignition::common::Time &_time, bool _force)
{
  for (auto &s : this->dataPtr->sensors)
  {
    s->Update(_time, _force);
  }
}

//////////////////////////////////////////////////
ignition::sensors::SensorId Manager::SensorId(const std::string &_name)
{
  // TODO find sensor id given sensor name
}