// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DEBUGGER_DEVTOOLS_CLIENT_HOST_H_
#define CHROME_BROWSER_DEBUGGER_DEVTOOLS_CLIENT_HOST_H_
#pragma once

#include <string>

#include "base/basictypes.h"

namespace IPC {
class Message;
}

class DevToolsWindow;
class TabContentsWrapper;

// Describes interface for managing devtools clients from browser process. There
// are currently two types of clients: devtools windows and TCP socket
// debuggers.
class DevToolsClientHost {
 public:
  class CloseListener {
   public:
    CloseListener() {}
    virtual ~CloseListener() {}
    virtual void ClientHostClosing(DevToolsClientHost* host) = 0;
   private:
    DISALLOW_COPY_AND_ASSIGN(CloseListener);
  };

  virtual ~DevToolsClientHost() {}

  // This method is called when tab inspected by this devtools client is
  // closing.
  virtual void InspectedTabClosing() = 0;

  // This method is called when tab inspected by this devtools client is
  // navigating to |url|.
  virtual void FrameNavigating(const std::string& url) = 0;

  // Sends the message to the devtools client hosted by this object.
  virtual void SendMessageToClient(const IPC::Message& msg) = 0;

  void set_close_listener(CloseListener* listener) {
    close_listener_ = listener;
  }

  virtual DevToolsWindow* AsDevToolsWindow();

  // Invoked when a tab is replaced by another tab. This is triggered by
  // TabStripModel::ReplaceTabContentsAt.
  virtual void TabReplaced(TabContentsWrapper* new_tab) = 0;

 protected:
  DevToolsClientHost() : close_listener_(NULL) {}

  // Should be called when the devtools client is going to die and this
  // DevToolsClientHost should not be used anymore.
  void NotifyCloseListener();

 private:
  CloseListener* close_listener_;
  DISALLOW_COPY_AND_ASSIGN(DevToolsClientHost);
};

#endif  // CHROME_BROWSER_DEBUGGER_DEVTOOLS_CLIENT_HOST_H_
