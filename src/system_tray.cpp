/**
 * @file system_tray.cpp
 */
// macros
#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1

  #if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <accctrl.h>
    #include <aclapi.h>
    #define TRAY_ICON WEB_DIR "images/favicon.ico"
  #elif defined(__linux__) || defined(linux) || defined(__linux)
    #define TRAY_ICON "sunshine"
  #elif defined(__APPLE__) || defined(__MACH__)
    #define TRAY_ICON WEB_DIR "images/logo-sunshine-16.png"
    #include <dispatch/dispatch.h>
  #endif

  // standard includes
  #include <csignal>
  #include <string>

  // lib includes
  #include "tray/tray.h"
  #include <boost/filesystem.hpp>
  #include <boost/process/environment.hpp>

  // local includes
  #include "confighttp.h"
  #include "main.h"
  #include "platform/common.h"
  #include "process.h"

using namespace std::literals;

// system_tray namespace
namespace system_tray {

  /**
 * @brief Open a url in the default web browser.
 * @param url The url to open.
 */
  void
  open_url(const std::string &url) {
    boost::filesystem::path working_dir;

    // if windows
  #if defined(_WIN32)
    // set working dir to Windows system directory
    working_dir = boost::filesystem::path(std::getenv("SystemRoot"));

    // this isn't ideal as it briefly shows a command window
    // but start a command built into cmd, not an executable
    std::string cmd = R"(cmd /C "start )" + url + R"(")";
  #elif defined(__linux__) || defined(linux) || defined(__linux)
    // set working dir to user home directory
    working_dir = boost::filesystem::path(std::getenv("HOME"));
    std::string cmd = R"(xdg-open ")" + url + R"(")";
  #elif defined(__APPLE__) || defined(__MACH__)
    std::string cmd = R"(open ")" + url + R"(")";
  #endif

    boost::process::environment _env = boost::this_process::environment();
    std::error_code ec;
    auto child = platf::run_unprivileged(cmd, working_dir, _env, nullptr, ec, nullptr);
    if (ec) {
      BOOST_LOG(warning) << "Couldn't open url ["sv << url << "]: System: "sv << ec.message();
    }
    else {
      BOOST_LOG(info) << "Opened url ["sv << url << "]"sv;
      child.detach();
    }
  }

  /**
 * @brief Callback for opening the UI from the system tray.
 * @param item The tray menu item.
 */
  void
  tray_open_ui_cb(struct tray_menu *item) {
    BOOST_LOG(info) << "Opening UI from system tray"sv;

    // create the url with the port
    std::string url = "https://localhost:" + std::to_string(map_port(confighttp::PORT_HTTPS));

    // open the url in the default web browser
    open_url(url);
  }

  /**
 * @brief Callback for opening GitHub Sponsors from the system tray.
 * @param item The tray menu item.
 */
  void
  tray_donate_github_cb(struct tray_menu *item) {
    open_url("https://github.com/sponsors/LizardByte");
  }

  /**
 * @brief Callback for opening MEE6 donation from the system tray.
 * @param item The tray menu item.
 */
  void
  tray_donate_mee6_cb(struct tray_menu *item) {
    open_url("https://mee6.xyz/m/804382334370578482");
  }

  /**
 * @brief Callback for opening Patreon from the system tray.
 * @param item The tray menu item.
 */
  void
  tray_donate_patreon_cb(struct tray_menu *item) {
    open_url("https://www.patreon.com/LizardByte");
  }

  /**
 * @brief Callback for opening PayPal donation from the system tray.
 * @param item The tray menu item.
 */
  void
  tray_donate_paypal_cb(struct tray_menu *item) {
    open_url("https://www.paypal.com/paypalme/ReenigneArcher");
  }

  /**
 * @brief Callback for exiting Sunshine from the system tray.
 * @param item The tray menu item.
 */
  void
  tray_quit_cb(struct tray_menu *item) {
    BOOST_LOG(info) << "Quiting from system tray"sv;

    std::raise(SIGINT);
  }

  // Tray menu
  static struct tray tray = {
    .icon = TRAY_ICON,
  #if defined(_WIN32)
    .tooltip = const_cast<char *>("Sunshine"),  // cast the string literal to a non-const char* pointer
  #endif
    .menu =
      (struct tray_menu[]) {
        // todo - use boost/locale to translate menu strings
        { .text = "Open Sunshine", .cb = tray_open_ui_cb },
        { .text = "-" },
        { .text = "Donate",
          .submenu =
            (struct tray_menu[]) {
              { .text = "GitHub Sponsors", .cb = tray_donate_github_cb },
              { .text = "MEE6", .cb = tray_donate_mee6_cb },
              { .text = "Patreon", .cb = tray_donate_patreon_cb },
              { .text = "PayPal", .cb = tray_donate_paypal_cb },
              { .text = nullptr } } },
        { .text = "-" },
        { .text = "Quit", .cb = tray_quit_cb },
        { .text = nullptr } },
  };

  /**
 * @brief Create the system tray.
 * @details This function has an endless loop, so it should be run in a separate thread.
 * @return 1 if the system tray failed to create, otherwise 0 once the tray has been terminated.
 */
  int
  system_tray() {
  #ifdef _WIN32
    // If we're running as SYSTEM, Explorer.exe will not have permission to open our thread handle
    // to monitor for thread termination. If Explorer fails to open our thread, our tray icon
    // will persist forever if we terminate unexpectedly. To avoid this, we will modify our thread
    // DACL to add an ACE that allows SYNCHRONIZE access to Everyone.
    {
      PACL old_dacl;
      PSECURITY_DESCRIPTOR sd;
      auto error = GetSecurityInfo(GetCurrentThread(),
        SE_KERNEL_OBJECT,
        DACL_SECURITY_INFORMATION,
        nullptr,
        nullptr,
        &old_dacl,
        nullptr,
        &sd);
      if (error != ERROR_SUCCESS) {
        BOOST_LOG(warning) << "GetSecurityInfo() failed: "sv << error;
        return 1;
      }

      auto free_sd = util::fail_guard([sd]() {
        LocalFree(sd);
      });

      SID_IDENTIFIER_AUTHORITY sid_authority = SECURITY_WORLD_SID_AUTHORITY;
      PSID world_sid;
      if (!AllocateAndInitializeSid(&sid_authority, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &world_sid)) {
        error = GetLastError();
        BOOST_LOG(warning) << "AllocateAndInitializeSid() failed: "sv << error;
        return 1;
      }

      auto free_sid = util::fail_guard([world_sid]() {
        FreeSid(world_sid);
      });

      EXPLICIT_ACCESS ea {};
      ea.grfAccessPermissions = SYNCHRONIZE;
      ea.grfAccessMode = GRANT_ACCESS;
      ea.grfInheritance = NO_INHERITANCE;
      ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
      ea.Trustee.ptstrName = (LPSTR) world_sid;

      PACL new_dacl;
      error = SetEntriesInAcl(1, &ea, old_dacl, &new_dacl);
      if (error != ERROR_SUCCESS) {
        BOOST_LOG(warning) << "SetEntriesInAcl() failed: "sv << error;
        return 1;
      }

      auto free_new_dacl = util::fail_guard([new_dacl]() {
        LocalFree(new_dacl);
      });

      error = SetSecurityInfo(GetCurrentThread(),
        SE_KERNEL_OBJECT,
        DACL_SECURITY_INFORMATION,
        nullptr,
        nullptr,
        new_dacl,
        nullptr);
      if (error != ERROR_SUCCESS) {
        BOOST_LOG(warning) << "SetSecurityInfo() failed: "sv << error;
        return 1;
      }
    }
  #endif

    if (tray_init(&tray) < 0) {
      BOOST_LOG(warning) << "Failed to create system tray"sv;
      return 1;
    }
    else {
      BOOST_LOG(info) << "System tray created"sv;
    }

    while (tray_loop(1) == 0) {
      BOOST_LOG(debug) << "System tray loop"sv;
    }

    return 0;
  }

  /**
 * @brief Run the system tray with platform specific options.
 * @note macOS requires that UI elements be created on the main thread, so the system tray is not implemented for macOS.
 */
  void
  run_tray() {
    // create the system tray
  #if defined(__APPLE__) || defined(__MACH__)
    // macOS requires that UI elements be created on the main thread
    // creating tray using dispatch queue does not work, although the code doesn't actually throw any (visible) errors

    // dispatch_async(dispatch_get_main_queue(), ^{
    //   system_tray();
    // });

    BOOST_LOG(info) << "system_tray() is not yet implemented for this platform."sv;
  #else  // Windows, Linux
    // create tray in separate thread
    std::thread tray_thread(system_tray);
    tray_thread.detach();
  #endif
  }

  /**
 * @brief Exit the system tray.
 * @return 0 after exiting the system tray.
 */
  int
  end_tray() {
    tray_exit();
    return 0;
  }

}  // namespace system_tray
#endif
