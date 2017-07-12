/*
 * Copyright 2010, The Android Open Source Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef Connection_h
#define Connection_h
#include "ActiveDOMObject.h"
#include "DOMStringList.h"
#include "Event.h"
#include "EventListener.h"
#include "EventNames.h"
#include "EventTarget.h"
#include <wtf/Forward.h>
#include <wtf/text/WTFString.h>
#include <wtf/PassRefPtr.h>
#include <wtf/RefCounted.h>
#include <limits>
#include <math.h>

namespace WebCore {

class Connection : public RefCounted<Connection>, public EventTarget, public ActiveDOMObject{  //SAMSUNG CHANGE - MPSG6020
public:
    enum ConnectionType {
        UNKNOWN = 0,
        ETHERNET = 1,
        WIFI = 2,
        CELL_2G = 3,
        CELL_3G = 4,
    };

//SAMSUNG CHANGE - MPSG6020 >>
     static PassRefPtr<Connection> create(ScriptExecutionContext* context) { 
		return adoptRef(new Connection(context)); 
	}
//SAMSUNG CHANGE - MPSG6020 <<

     ConnectionType type() const;

//SAMSUNG CHANGE - MPSG6020 >>

     bool metered() const;
     double bandwidth() const;


	 
	 DEFINE_ATTRIBUTE_EVENT_LISTENER(change);
	 
     virtual Connection* toConnection() { return this; }
	 virtual ScriptExecutionContext* scriptExecutionContext() const;
	 	void fireEvent(const AtomicString& type); 


    // ActiveDOMObject
    virtual bool hasPendingActivity() const;
    virtual bool canSuspend() const;
    virtual void stop() const;

    using RefCounted<Connection>::ref;
    using RefCounted<Connection>::deref;

//SAMSUNG CHANGE - MPSG6020 <<

private:

//SAMSUNG CHANGE - MPSG6020 >>
    Connection(ScriptExecutionContext* context):ActiveDOMObject(context, this) { }
	

     


    // EventTarget
    virtual void refEventTarget() { ref(); }
    virtual void derefEventTarget() { deref(); }
    virtual EventTargetData* eventTargetData() { return &m_eventTargetData; }
    virtual EventTargetData* ensureEventTargetData() { return &m_eventTargetData; }
 
	
    EventTargetData m_eventTargetData;
//SAMSUNG CHANGE - MPSG6020 <<
};

} // namespace WebCore

#endif // Connection_h
