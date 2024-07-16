#include "ServiceController.h"

#include <locale>
#include <codecvt>
#include <cstring>

#ifdef WIN32
#include <Windows.h>
#include <direct.h>
#endif

static std::string getError()
{
#ifdef WIN32

    LPSTR errorText = nullptr;

    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER |FORMAT_MESSAGE_IGNORE_INSERTS,
                  nullptr,
                  GetLastError(),
                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  reinterpret_cast<LPSTR>(&errorText),
                  0,
                  nullptr);

    if(errorText == nullptr) return std::string();

    std::string ret(errorText);

    LocalFree(errorText);

    return ret;

#else
    return std::string();
#endif
}

static std::vector<std::string> getArgs()
{
     std::vector<std::string> list;

#ifdef WIN32
     char * str = GetCommandLineA();
#endif

     for(char * end = str + strlen(str); str < end; str++)
     {
        if(*str != ' ')
        {
           int cnt = 0;

           if(*str == '\"')
           {
              int i = 0;

              for(char * temp = str + 1; temp < end; temp++)
              {
                  i++;

                  if(*temp == '\"')
                  {
                     temp++;

                     if(temp == end || *temp == ' ')
                     {
                        cnt = i;
                        break;
                     }
                     else i++;
                  }
              }
           }

           if(cnt == 0)
           {
              std::string data;

              do
              {
                data.push_back(*str);
                str++;
              }
              while(str != end && *str != ' ');

              list.push_back(data);
           }
           else
           {
              std::string data(str + 1, cnt - 1);
              list.push_back(data);
              str += cnt;
           }
        }
     }

     return list;
}

//=================================================================

ServiceController * ServiceController::serviceController = nullptr;

#ifdef WIN32

void ServiceController::ControlHandler(unsigned long  request)
{
     switch(request)
     {
       case SERVICE_CONTROL_STOP:
       case SERVICE_CONTROL_SHUTDOWN:
       {
            std::lock_guard<std::mutex> lock(serviceController->mutex);

            serviceController->executeStop();

            serviceController->wait.wait(false);

            serviceController->serviceStatus->dwCurrentState = SERVICE_STOPPED;

            if(!SetServiceStatus(serviceController->handleStatus, serviceController->serviceStatus))
            {
               if(serviceController->logger) serviceController->logger("Service status set error: " + getError());
            }
       }
       break;
       default:
       break;
     }
}

void ServiceController::ServiceMain(int argc, char** argv)
{
     if(serviceController == nullptr) return;

     serviceController->handleStatus = RegisterServiceCtrlHandler(std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(serviceController->serviceName.data()).data(), static_cast<LPHANDLER_FUNCTION>(ControlHandler));
     if(serviceController->handleStatus == static_cast<SERVICE_STATUS_HANDLE>(0)) return;

     serviceController->serviceStatus->dwServiceType = SERVICE_WIN32_OWN_PROCESS;
     serviceController->serviceStatus->dwCurrentState = SERVICE_RUNNING;
     serviceController->serviceStatus->dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
     serviceController->serviceStatus->dwWin32ExitCode = 0;
     serviceController->serviceStatus->dwServiceSpecificExitCode = 0;
     serviceController->serviceStatus->dwCheckPoint = 0;
     serviceController->serviceStatus->dwWaitHint = 0;

     if(!SetServiceStatus(serviceController->handleStatus, serviceController->serviceStatus))
     {
        if(serviceController->logger) serviceController->logger("Service status set error: " + getError());
     }

     std::vector<std::string> list;
     for(int i = 0; i < argc; i++) list.push_back(argv[i]);
     serviceController->serviceStatus->dwWin32ExitCode = serviceController->start(list, true);

     serviceController->wait = true;
     serviceController->wait.notify_one();

     std::lock_guard<std::mutex> lock(serviceController->mutex);

     if(serviceController->serviceStatus->dwCurrentState != SERVICE_STOPPED)
     {
        serviceController->serviceStatus->dwCurrentState = SERVICE_STOPPED;

        if(!SetServiceStatus(serviceController->handleStatus, serviceController->serviceStatus))
        {
           if(serviceController->logger) serviceController->logger("Service status set error: " + getError());
        }
     }
}

bool ServiceController::InstallSvc(bool autoStart)
{
     if(logger) logger("Service install");

     SC_HANDLE manager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);

     if(manager == nullptr)
     {
        if(logger) logger("Service manager opening error: " + getError());

        return false;
     }

     char servicePath[_MAX_PATH+1];
     GetModuleFileNameA(nullptr, servicePath, _MAX_PATH);

     SC_HANDLE service = CreateServiceA(manager,
                                        serviceName.data(),
                                        serviceName.data(),
                                        SERVICE_ALL_ACCESS,
                                        SERVICE_WIN32_OWN_PROCESS,
                                        (autoStart) ? SERVICE_AUTO_START : SERVICE_DEMAND_START,
                                        SERVICE_ERROR_NORMAL,
                                        servicePath,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        nullptr);

     if(service == nullptr)
     {
        if(logger) logger("Service create error: " + getError());

        CloseServiceHandle(manager);
        return false;
     }

     SERVICE_DESCRIPTIONA description = {serviceDescription.data()};

     if(!ChangeServiceConfig2A(service, SERVICE_CONFIG_DESCRIPTION, &description))
     {
        if(logger) logger("Service description set error: " + getError());

        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return false;
     }

     CloseServiceHandle(service);
     CloseServiceHandle(manager);

     if(logger) logger("Service installed successfully");

     return true;
}

bool ServiceController::RemoveSvc()
{
     if(logger) logger("Service remove");

     SC_HANDLE manager = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);

     if(manager == nullptr)
     {
        if(logger) logger("Service manager opening error: " + getError());

        return false;
     }

     SC_HANDLE service = OpenServiceA(manager, serviceName.data(), SERVICE_STOP | DELETE);

     if(service == nullptr)
     {
        if(logger) logger("Service opening error: " + getError());

        CloseServiceHandle(manager);
        return false;
     }

     DeleteService(service);
     CloseServiceHandle(service);
     CloseServiceHandle(manager);

     if(logger) logger("Service removed successfully");

     return true;
}

bool ServiceController::StartSvc()
{
     if(logger) logger("Service start");

     SC_HANDLE manager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);

     if(manager == nullptr)
     {
        if(logger) logger("Service manager opening error: " + getError());

        return false;
     }

     SC_HANDLE service = OpenServiceA(manager, serviceName.data(), SERVICE_START);

     if(service == nullptr)
     {
        if(logger) logger("Service opening error: " + getError());

        CloseServiceHandle(manager);
        return false;
     }

     if(!StartServiceA(service, 0, nullptr))
     {
        if(logger) logger("Service starting error: " + getError());

        CloseServiceHandle(manager);
        return false;
     }

     CloseServiceHandle(service);
     CloseServiceHandle(manager);

     if(logger) logger("Service started successfully");

     return true;
}

bool ServiceController::StopSvc()
{
     if(logger) logger("Service stop");

     SC_HANDLE manager = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);

     if(manager == nullptr)
     {
        if(logger) logger("Service manager opening error: " + getError());

        return false;
     }

     SC_HANDLE service = OpenServiceA(manager, serviceName.data(), SERVICE_STOP | SERVICE_QUERY_STATUS | SERVICE_ENUMERATE_DEPENDENTS);

     if(service == nullptr)
     {
        if(logger) logger("Service opening error: " + getError());

        CloseServiceHandle(manager);
        return false;
     }

     auto getInfo = [manager, service, this](SERVICE_STATUS_PROCESS * ssp) -> bool {

          unsigned long dwBytesNeeded;

          if(!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(SERVICE_STATUS_PROCESS), &dwBytesNeeded))
          {
             if(logger) logger("Service status query error: " + getError());

             CloseServiceHandle(service);
             CloseServiceHandle(manager);

             return false;
          }

          return true;
     };

     SERVICE_STATUS_PROCESS ssp;

     if(!getInfo(&ssp)) return false;

     if(ssp.dwCurrentState == SERVICE_STOPPED)
     {
        if(logger) logger("Service is already stopped");
        return true;
     }

     const unsigned long timeout = 30000, waitTime = 1000;
     unsigned long startTime = GetTickCount();

     if(!ControlService(service, SERVICE_CONTROL_STOP, reinterpret_cast<LPSERVICE_STATUS>(&ssp)))
     {
        if(logger) logger("Service control error: " + getError());

        CloseServiceHandle(service);
        CloseServiceHandle(manager);

        return false;
     }

     while(ssp.dwCurrentState != SERVICE_STOPPED)
     {
           Sleep(waitTime);

           if(!getInfo(&ssp)) return false;

           if(ssp.dwCurrentState == SERVICE_STOPPED) break;

           if(GetTickCount() - startTime > timeout )
           {
              if(logger) logger("Service stop timed out");

              CloseServiceHandle(service);
              CloseServiceHandle(manager);

              return false;
           }
     }

     CloseServiceHandle(service);
     CloseServiceHandle(manager);

     if(logger) logger("Service stopped successfully");

     return true;
}

#endif

void ServiceController::executeStop()
{
     while(!stopList.empty())
     {
           if(stopList.top()) stopList.top()();
           stopList.pop();
     }

     if(stop) stop();
}

ServiceController::ServiceController(std::string_view serviceName, std::string_view serviceDescription, const std::function<void(std::string_view)> & logger)
    : serviceName(serviceName), serviceDescription(serviceDescription), logger(logger)
{
     if(serviceController != nullptr) return;
     serviceController = this;
     serviceStatus = new _SERVICE_STATUS;
}

ServiceController::~ServiceController()
{
     if(serviceStatus != nullptr) delete serviceStatus;
}

std::string ServiceController::appWorkFolder()
{
#ifdef WIN32

     char workPath[MAX_PATH + 1] = {0};
     GetModuleFileNameA(NULL, workPath, MAX_PATH);
     char * lstChr = strrchr(workPath, '\\');
     * lstChr = '\0';

     char currentPath[MAX_PATH + 1] = {0};
     GetCurrentDirectoryA(MAX_PATH, currentPath);

     if(std::strcmp(workPath, currentPath) != 0) return std::string(workPath);

#endif

     return std::string();
}

std::string ServiceController::appWorkFolderJoin(const std::string & fileNameOrFolder)
{
     std::string path = ServiceController::appWorkFolder();

     if(path.size() == 0) return fileNameOrFolder;

#ifdef WIN32
     path += '\\';
#else
     path += '/';
#endif

     return path + fileNameOrFolder;
}

std::string ServiceController::appWorkFolderJoin(const std::vector<std::string> & fileNamesOrFolders)
{
     std::string path = ServiceController::appWorkFolder();

     if(path.size() > 0)
     {
#ifdef WIN32
        path += '\\';
#else
        path += '/';
#endif
     }

     for(unsigned int i = 0; i < fileNamesOrFolders.size(); i++)
     {
         if(i > 0)
         {
#ifdef WIN32
            path += '\\';
#else
            path += '/';
#endif
         }

         path += fileNamesOrFolders[i];
     }

     return path;
}

bool ServiceController::isConsole()
{
#ifdef WIN32
     return GetConsoleWindow() != nullptr;
#else
     return true;
#endif
}

const std::function<void()> ServiceController::stopFuncCallback()
{
     return (serviceController != this) ? std::function<void()>() : std::bind(&ServiceController::executeStop, this);
}

void ServiceController::addStopListCallback(const std::function<void()> & stopFunc)
{
     stopList.push(stopFunc);
}

void ServiceController::clearStopList()
{
     while(!stopList.empty()) stopList.pop();
}

void ServiceController::run(const std::function<int(const std::vector<std::string> &, bool)> &startFunc, const std::function<void()> & stopFunc, bool cmdInstallService)
{
     if(serviceController != this) return;

     if(!startFunc || !stopFunc) return;
     if(isInit) return;

     isInit = true;

#ifdef WIN32

     if(!isConsole())
     {
        start = startFunc;
        stop = stopFunc;

        SERVICE_TABLE_ENTRYA ServiceTable[] = {{const_cast<char *>(serviceName.data()), reinterpret_cast<LPSERVICE_MAIN_FUNCTIONA>(&ServiceController::ServiceMain)}, {nullptr, nullptr}};

        if(!StartServiceCtrlDispatcherA(ServiceTable))
        {
           if(logger) logger("Service control dispatcher starting error, code: " + std::to_string(GetLastError()) + "  " + getError());
        }
     }
     else
     {

#endif
        std::vector<std::string> args = getArgs();

        if(cmdInstallService && args.size() == 3)
        {
           if(args[1] == "service")
           {
              if(args[2] == "install")
              {
                 installService();
                 return;
              }

              if(args[2] == "remove")
              {
                 removeService();
                 return;
              }

              if(args[2] == "start")
              {
                 startService();
                 return;
              }

              if(args[2] == "stop")
              {
                 stopService();
                 return;
              }
           }
        }

        stop = stopFunc;
        startFunc(args, false);

#ifdef WIN32

     }
#endif

}

bool ServiceController::installService(bool autoStart)
{
     if(serviceController != this) return false;
#ifdef WIN32
     return InstallSvc(autoStart);
#else
     return false;
#endif
}

bool ServiceController::removeService()
{
     if(serviceController != this) return false;
#ifdef WIN32
     return RemoveSvc();
#else
     return false;
#endif
}

bool ServiceController::startService()
{
     if(serviceController != this) return false;
#ifdef WIN32
     return StartSvc();
#else
     return false;
#endif
}

bool ServiceController::stopService()
{
     if(serviceController != this) return false;
#ifdef WIN32
     return StopSvc();
#else
     return false;
#endif
}
