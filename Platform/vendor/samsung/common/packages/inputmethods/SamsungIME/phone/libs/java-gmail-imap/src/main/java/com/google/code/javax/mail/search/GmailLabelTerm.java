package com.google.code.javax.mail.search;

import com.google.code.com.sun.mail.imap.IMAPMessage;
import com.google.code.javax.mail.Message;

public final class GmailLabelTerm extends GmailSearchTerm {

    protected final static String searchAttribute = "X-GM-LABELS";

    /**
     * Constructor.
     *
     * @param gm_msgid  the gm_msgid to search for
     */
    public GmailLabelTerm(String gm_label) {
        // Note: comparison is case-insensitive
        super(gm_label);
    }

    /**
     * The match method.
     *
     * @param msg	the match is applied to this Message's 
     *			Message-ID header
     * @return		true if the match succeeds, otherwise false
     */
    public boolean match(Message msg) {
        String[] gm_labels = null;
        try {
            IMAPMessage im = (IMAPMessage) msg;
            gm_labels = im.getGoogleMessageLabels();
        } catch (Exception e) {
            return false;
        }
        if (gm_labels == null) {
            return false;
        }

        for (int i = 0; i < gm_labels.length; i++) {
            if (super.match(gm_labels[i])) {
                return true;
            }
        }
        return false;
    }

    /**
     * Equality comparison.
     */
    @Override
    public boolean equals(Object obj) {
        if (!(obj instanceof GmailThreadIDTerm)) {
            return false;
        }
        return super.equals(obj);
    }

    @Override
    public String getSearchAttribute() {
        return searchAttribute;
    }
}
