package com.google.code.javax.mail.search;

import com.google.code.com.sun.mail.imap.IMAPMessage;
import com.google.code.javax.mail.Message;

public final class GmailMessageIDTerm extends GmailSearchTerm {


    protected final static String searchAttribute = "X-GM-MSGID";
    
    /**
     * Constructor.
     *
     * @param gm_msgid  the gm_msgid to search for
     */
    public GmailMessageIDTerm(String gm_msgid) {
	// Note: comparison is case-insensitive
	super(gm_msgid);
    }

    /**
     * The match method.
     *
     * @param msg	the match is applied to this Message's 
     *			Message-ID header
     * @return		true if the match succeeds, otherwise false
     */
    public boolean match(Message msg) {
        String gm_msgid = null;
	try {
            IMAPMessage im = (IMAPMessage) msg;
            gm_msgid = Long.toHexString(im.getGoogleMessageId());
	} catch (Exception e) {
	    return false;
	}
	if (gm_msgid == null)
	    return false;
        
        if(super.match(gm_msgid)){
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
