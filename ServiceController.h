#ifndef SERVICECONTROLLER_H
#define SERVICECONTROLLER_H

#include <string>
#include <functional>
#include <string>
#include <stack>

#ifdef WIN32
#include <atomic>
#include <mutex>
#endif

class ServiceController final
{
      std::string serviceName, serviceDescription;
      std::function<void(std::string_view)> logger;
      std::function<int(const std::vector<std::string> &, bool)> start;

      std::stack<std::function<void()>> stopList;
      std::function<void()> stop;

      bool isInit = false;
      static ServiceController * serviceController;

#ifdef WIN32

      std::mutex mutex;
      std::atomic_bool wait = false;

      struct _SERVICE_STATUS * serviceStatus = nullptr;
      struct SERVICE_STATUS_HANDLE__ * handleStatus;

      static void ControlHandler(unsigned long  request);
      static void ServiceMain(int argc, char** argv);
      bool InstallSvc(bool autoStart);
      bool RemoveSvc();
      bool StartSvc();
      bool StopSvc();

#endif

      void executeStop();

public:
      ServiceController() = delete;
      explicit ServiceController(std::string_view serviceName, std::string_view serviceDescription, const std::function<void(std::string_view)> & logger = nullptr);
      ~ServiceController();

      static std::string appWorkFolder();
      static std::string appWorkFolderJoin(const std::string & fileNameOrFolder);
      static std::string appWorkFolderJoin(const std::vector<std::string> & fileNamesOrFolders);

      static bool isConsole();
      const std::function<void()> stopFuncCallback();

      void addStopListCallback(const std::function<void()> & stopFunc);
      void clearStopList();

      void run(const std::function<int(const std::vector<std::string> &, bool)> & startFunc, const std::function<void()> & stopFunc, bool cmdInstallService = true);
      bool installService(bool autoStart = true);
      bool removeService();
      bool startService();
      bool stopService();
};

#endif // SERVICECONTROLLER_H
