package com.google.code.javax.mail.search;

import com.google.code.com.sun.mail.imap.IMAPMessage;
import com.google.code.javax.mail.Message;

public final class GmailThreadIDTerm extends GmailSearchTerm {

    protected final static String searchAttribute = "X-GM-THRID";
    
    /**
     * Constructor.
     *
     * @param thrdid  the thrdid to search for
     */
    public GmailThreadIDTerm(String thrdid) {
	// Note: comparison is case-insensitive
	super(thrdid);
    }

    /**
     * The match method.
     *
     * @param msg	the match is applied to this Message's 
     *			Message-ID header
     * @return		true if the match succeeds, otherwise false
     */
    public boolean match(Message msg) {
        String thrdid = null;
	try {
            IMAPMessage im = (IMAPMessage) msg;
            thrdid = Long.toHexString(im.getGoogleMessageThreadId());
	} catch (Exception e) {
	    return false;
	}
	if (thrdid == null)
	    return false;
        
        if(super.match(thrdid)){
            return true;
        }
        return false;
    }

    /**
     * Equality comparison.
     */
    @Override
    public boolean equals(Object obj) {
	if (!(obj instanceof GmailThreadIDTerm))
	    return false;
	return super.equals(obj);
    }

    @Override
    public String getSearchAttribute() {
        return searchAttribute;
    }

}
