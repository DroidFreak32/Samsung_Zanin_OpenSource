package org.cups4j.operations.ipp;

/**
 * Copyright (C) 2009 Harald Weyhing
 * 
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU Lesser General Public License as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * See the GNU Lesser General Public License for more details. You should have received a copy of
 * the GNU Lesser General Public License along with this program; if not, see
 * <http://www.gnu.org/licenses/>.
 */
import java.io.UnsupportedEncodingException;
import java.net.URL;
import java.nio.ByteBuffer;
import java.util.HashMap;
import java.util.Map;

import org.cups4j.JobStateEnum;
import org.cups4j.PrintJobAttributes;
import org.cups4j.operations.IppOperation;

import ch.ethz.vppserver.ippclient.IppResult;
import ch.ethz.vppserver.ippclient.IppTag;
import ch.ethz.vppserver.schema.ippclient.Attribute;
import ch.ethz.vppserver.schema.ippclient.AttributeGroup;

public class IppGetJobAttributesOperation extends IppOperation {

  public IppGetJobAttributesOperation() {
    operationID = 0x0009;
    bufferSize = 8192;
  }

  public IppGetJobAttributesOperation(int port) {
    this();
    this.ippPort = port;
  }

  /**
   * 
   * @param uri
   *          printer-uri or job-uri
   * @param map
   *          attributes i.e. job-id,requesting-user-name,requested-attributes
   * @return ByteBuffer IPP header
   * @throws UnsupportedEncodingException
   */
  public ByteBuffer getIppHeader(URL uri, Map<String, String> map) throws UnsupportedEncodingException {
    ByteBuffer ippBuf = ByteBuffer.allocateDirect(bufferSize);
    ippBuf = IppTag.getOperation(ippBuf, operationID);

    if (map == null) {
      ippBuf = IppTag.getUri(ippBuf, "job-uri", stripPortNumber(uri));
      ippBuf = IppTag.getEnd(ippBuf);
      ippBuf.flip();
      return ippBuf;
    }

    if (map.get("job-id") == null) {
      ippBuf = IppTag.getUri(ippBuf, "job-uri", stripPortNumber(uri));
    } else {
      ippBuf = IppTag.getUri(ippBuf, "printer-uri", stripPortNumber(uri));
      int jobId = Integer.parseInt(map.get("job-id"));
      ippBuf = IppTag.getInteger(ippBuf, "job-id", jobId);
    }

    ippBuf = IppTag.getNameWithoutLanguage(ippBuf, "requesting-user-name", map.get("requesting-user-name"));

    if (map.get("requested-attributes") != null) {
      String[] sta = map.get("requested-attributes").split(" ");
      if (sta != null) {
        ippBuf = IppTag.getKeyword(ippBuf, "requested-attributes", sta[0]);
        int l = sta.length;
        for (int i = 1; i < l; i++) {
          ippBuf = IppTag.getKeyword(ippBuf, null, sta[i]);
        }
      }
    }

    if (map.get("which-jobs") != null) {
      ippBuf = IppTag.getKeyword(ippBuf, "which-jobs", map.get("which-jobs"));
    }

    if (map.get("my-jobs") != null) {
      boolean value = false;
      if (map.get("my-jobs").equals("true")) {
        value = true;
      }
      ippBuf = IppTag.getBoolean(ippBuf, "my-jobs", value);
    }
    ippBuf = IppTag.getEnd(ippBuf);
    ippBuf.flip();
    return ippBuf;
  }

  public PrintJobAttributes getPrintJobAttributes(String hostname, String userName, int port, int jobID)
      throws Exception {
    PrintJobAttributes job = null;

    Map<String, String> map = new HashMap<String, String>();
    map.put("requested-attributes",
        "page-ranges print-quality sides job-uri job-id job-state job-printer-uri job-name job-originating-user-name");
    map.put("requesting-user-name", userName);
    IppResult result = request(new URL("http://" + hostname + "/jobs/" + jobID), map);

    // IppResultPrinter.print(result);
    for (AttributeGroup group : result.getAttributeGroupList()) {
      if ("job-attributes-tag".equals(group.getTagName())) {
        job = new PrintJobAttributes();
        for (Attribute attr : group.getAttribute()) {
          if (attr.getAttributeValue() != null && !attr.getAttributeValue().isEmpty()) {
            String attValue = getAttributeValue(attr);
            ;
            if ("job-uri".equals(attr.getName())) {
              job.setJobURL(new URL(attValue.replace("ipp://", "http://")));
            } else if ("job-id".equals(attr.getName())) {
              job.setJobID(Integer.parseInt(attValue));
            } else if ("job-state".equals(attr.getName())) {
              job.setJobState(JobStateEnum.fromString(attValue));
            } else if ("job-printer-uri".equals(attr.getName())) {
              job.setPrinterURL(new URL(attValue.replace("ipp://", "http://")));
            } else if ("job-name".equals(attr.getName())) {
              job.setJobName(attValue);
            } else if ("job-originating-user-name".equals(attr.getName())) {
              job.setUserName(attValue);
            }
          }
        }
      }
    }
    return job;
  }

}
