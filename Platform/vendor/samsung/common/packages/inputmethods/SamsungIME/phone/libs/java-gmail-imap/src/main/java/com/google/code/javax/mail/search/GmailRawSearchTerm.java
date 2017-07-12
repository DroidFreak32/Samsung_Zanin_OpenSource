package com.google.code.javax.mail.search;

import com.google.code.javax.mail.Message;

public final class GmailRawSearchTerm extends GmailSearchTerm {

    protected final static String searchAttribute = "X-GM-RAW";
    
    public GmailRawSearchTerm(String gm_raw) {
	// Note: comparison is case-insensitive
	super(gm_raw);
    }


    /**
     * Equality comparison.
     */
    @Override
    public boolean equals(Object obj) {
	if (!(obj instanceof GmailRawSearchTerm))
	    return false;
	return super.equals(obj);
    }

    @Override
    public String getSearchAttribute() {
        return searchAttribute;
    }

    @Override
    public boolean match(Message msg) {
        throw new UnsupportedOperationException("Not supported yet.");
    }

}
