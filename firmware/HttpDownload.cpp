#include "HttpDownload.h"
#include "SD.h"

#define TIMEOUT_INITIAL_RESPONSE 5000
#define TIMEOUT_RESPONSE_READ 5000
#define DEFAULT_CHUNK_SIZE 900
#define DEFAULT_RETRY_ATTEMPTS 3
#define DEFAULT_RETRY_TIMEOUT 500

#define HTTP_DOWNLOAD_METHOD "GET"


extern char* itoa(int a, char* buffer, unsigned char radix);

/**
* Constructor.
*/
HttpDownload::HttpDownload() {
  mBytesPerChunk = DEFAULT_CHUNK_SIZE;
  mLoggingLevel = LOGGING_LEVEL_NONE;
  mRetryAttempts = DEFAULT_RETRY_ATTEMPTS;
  mRetryTimeout = DEFAULT_RETRY_TIMEOUT;
}

HttpDownload::HttpDownload(int logLevel, int bytesPerChunk, int retryAttempts, int retryTimeout) {
  mBytesPerChunk = bytesPerChunk;
  mLoggingLevel = logLevel;
  mRetryAttempts = retryAttempts;
  mRetryTimeout = retryTimeout;
}


/**
* Method to send an HTTP Request. Allocate variables in your application code
* in the aResponse struct and set the headers and the options in the aRequest
* struct.
*/
void HttpDownload::request(HttpDownloadRequest &aRequest, HttpDownloadResponse &aResponse, HttpDownloadHeader headers[], void(*callback)(byte*, int, void*), void* callbackParam)
{
  int byteRangeStart = 0;
  int lastHttpStatus = 206;
  int retryCount = 0;

  while (lastHttpStatus == 206 || lastHttpStatus == -2) { //-2 is a retry request
    lastHttpStatus = -1; //assume failure

    sendRequest(aRequest, headers, byteRangeStart);

    lastHttpStatus = receiveResponse(aResponse, callback, callbackParam);
    if (lastHttpStatus == -1) {
        retryCount++;
        if (retryCount > mRetryAttempts) {
          //Give up.
          logLine(LOGGING_LEVEL_ERROR, "Giving up after max retries.");
        } else {
          //Delay, and ask for a retry
          logLine(LOGGING_LEVEL_INFO, "Last connection failed.  Retrying.");
          delay(mRetryTimeout);
          lastHttpStatus = -2;
        }
    } else {
        //Good response.  Clear the retry count
        retryCount = 0;
        byteRangeStart += mBytesPerChunk;
    }
  }
}

void HttpDownload::sendRequest(HttpDownloadRequest &request, HttpDownloadHeader headers[], int byteRangeStart) {

  // NOTE: The default port tertiary statement is unpredictable if the request structure is not initialised
  // http_request_t request = {0} or memset(&request, 0, sizeof(http_request_t)) should be used
  // to ensure all fields are zero
  bool connected = false;
  if(request.hostname!=NULL) {
    connected = client.connect(request.hostname, (request.port) ? request.port : 80 );
  }   else {
    connected = client.connect(request.ip, request.port);
  }

  if (connected) {
    if(request.hostname!=NULL) {
      log(LOGGING_LEVEL_DEBUG, "Connecting to: ");
      log(LOGGING_LEVEL_DEBUG, request.hostname);
    } else {
      log(LOGGING_LEVEL_DEBUG, "Connecting to IP address.");
    }
    log(LOGGING_LEVEL_DEBUG, ":");
    logLine(LOGGING_LEVEL_DEBUG, request.port);
  } else {
    log(LOGGING_LEVEL_DEBUG, "Connection failed.");
  }

  if (!connected) {
    client.stop();
    // If TCP Client can't connect to host, exit here.
    return;
  }

  sendHeaders(request, headers, byteRangeStart);

  logLine(LOGGING_LEVEL_DEBUG, "End of HTTP Request.");

}


void HttpDownload::sendHeaders(HttpDownloadRequest &request, HttpDownloadHeader headers[], int byteRangeStart) {
  //
  // Send HTTP Headers
  //

  // Send initial headers (only HTTP 1.0 is supported for now).
  out(HTTP_DOWNLOAD_METHOD);
  out(" ");
  out(request.path);
  out(" HTTP/1.1\r\n");

  logLine(LOGGING_LEVEL_DEBUG, "Start of HTTP Request.");
  log(LOGGING_LEVEL_DEBUG, HTTP_DOWNLOAD_METHOD);
  log(LOGGING_LEVEL_DEBUG, " ");
  log(LOGGING_LEVEL_DEBUG, request.path);
  logLine(LOGGING_LEVEL_DEBUG, " HTTP/1.1");

  // Send General and Request Headers.
  sendHeader("Connection", "close"); // Not supporting keep-alive for now.
  char rangeValue[64];
  char itoaBuffer[32];
  strcpy(rangeValue, "bytes=");
  itoa(byteRangeStart, itoaBuffer, 10);
  strcat(rangeValue, itoaBuffer);
  strcat(rangeValue, "-");
  itoa(byteRangeStart + mBytesPerChunk, itoaBuffer, 10);
  strcat(rangeValue, itoaBuffer);
  sendHeader("Range", rangeValue);
  if(request.hostname!=NULL) {
    sendHeader("HOST", request.hostname);
  }

  //Send custom headers
  if (headers != NULL) {
    int i = 0;
    while (headers[i].header != NULL) {
      if (headers[i].value != NULL) {
        sendHeader(headers[i].header, headers[i].value);
      } else {
        sendHeader(headers[i].header);
      }
      i++;
    }
  }

  // Empty line to finish headers
out("\r\n");
client.flush();
}

int HttpDownload::receiveResponse(HttpDownloadResponse &response,void(*callback)(byte*, int, void*), void* callbackParam) {
  // If a proper response code isn't received it will be set to -1.
  response.status = -1;

  //
  // Receive HTTP Response
  //
  // The first value of client.available() might not represent the
  // whole response, so after the first chunk of data is received instead
  // of terminating the connection there is a delay and another attempt
  // to read data.
  // The loop exits when the connection is closed, or if there is a
  // timeout or an error.

  unsigned long lastTime = millis();

  // Wait until the server either disconnects, gives us a response, or the timeout is exceeded
  logLine(LOGGING_LEVEL_DEBUG, "Waiting a for response.");
  while(client.connected() && !client.available() && millis() - lastTime < TIMEOUT_INITIAL_RESPONSE) {
    // While we are waiting, which could be a while, allow cloud stuff to happen as needed
    Spark.process();
  }

  if(client.connected() && client.available()) {
    // We stopped waiting because we actually got something
    int charCount = 0;
    int lastReadTime = millis();

    // Keep going until the server either disconnects, or stops feeding us data in a timely manner
    logLine(LOGGING_LEVEL_DEBUG, "Reading response.");
    bool headersEnded = false;
    // char headerBuffer[1024];
    // headerBuffer[0] = 0;
    while(client.connected() && millis() - lastReadTime < TIMEOUT_RESPONSE_READ) {
      // We got something!
      int numAvail = client.available();
      int packetSize = numAvail;
      if (numAvail > 0) {
        log(LOGGING_LEVEL_DEBUG, "Bytes available: ");
        logLine(LOGGING_LEVEL_DEBUG, numAvail);
        byte* availBuff = new byte[numAvail];
        client.read(availBuff, numAvail);

        if (response.status == -1) {
          char httpStatus[4];
          httpStatus[0] = availBuff[9];
          httpStatus[1] = availBuff[10];
          httpStatus[2] = availBuff[11];
          httpStatus[3] = 0;
          response.status = atoi(httpStatus);

          if (response.status == 416) {
            //All done.
            return response.status;
          }
        }

        // Make note of when this happened
        lastReadTime = millis();

        byte* callbackData = availBuff;
        //Eat the headers
        if (!headersEnded) {
          logLine(LOGGING_LEVEL_DEBUG, "Processing headers");

          char* headerEnd = strstr((const char*)availBuff, "\r\n\r\n");

          // strncat(headerBuffer, (char*)availBuff, (byte*)headerEnd - availBuff);
          if (headerEnd != NULL) {
            headerEnd += 4;// \r\n\r\n from above

            callbackData = (byte*)headerEnd;
            numAvail -= (byte*)headerEnd - availBuff;

            headersEnded = true;

          } else {
            //Still haven't found headers.  Don't send anything to the callback.
            numAvail = 0;
          }

        }

        charCount += packetSize;

        //Send the data to our callback
        if (numAvail != 0) {
          if (callback != NULL) {
            callback(callbackData, numAvail, callbackParam);
          }
        }
        delete availBuff;
      }

      // While we are waiting, which could be a while, allow cloud stuff to happen as needed
      Spark.process();

    }

    // Show a wait/read summary for debugging
    log(LOGGING_LEVEL_DEBUG, "Bytes read: ");
    logLine(LOGGING_LEVEL_DEBUG, charCount);
  }
  else {
    //We timed out on the initial response
    logLine(LOGGING_LEVEL_ERROR, "No/empty response");
  }

  // Just to be nice, let's make sure we finish clean
  client.flush();
  client.stop();
  logLine(LOGGING_LEVEL_DEBUG, "Connection closed");

  return response.status;
}

void HttpDownload::download(HttpDownloadRequest &aRequest, HttpDownloadResponse &response, char* filename) {
    File outFile;
    outFile = SD.open(filename, FILE_WRITE);

    request(aRequest, response, NULL, writeToFile, &outFile);
    outFile.close();
}

void HttpDownload::download(HttpDownloadRequest &aRequest, HttpDownloadResponse &aResponse, HttpDownloadHeader headers[], void(*callback)(byte*, int, void*), void* callbackParam) {
    request(aRequest, aResponse, headers, callback, callbackParam);
}

void HttpDownload::writeToFile(byte* fromResponse, int size, void* fileObject) {
  File* outFile = (File*)fileObject;
  outFile->write(fromResponse, size);
}

//client.print is extremely slow.  Use this instead.
void HttpDownload::out(const char *s) {
  client.write( (const uint8_t*)s, strlen(s) );
}

/**
* Method to send a header, should only be called from within the class.
*/
void HttpDownload::sendHeader(const char* aHeaderName, const char* aHeaderValue)
{
  out(aHeaderName);
  out(": ");
  out(aHeaderValue);
  out("\r\n");

  log(LOGGING_LEVEL_DEBUG, aHeaderName);
  log(LOGGING_LEVEL_DEBUG, ": ");
  logLine(LOGGING_LEVEL_DEBUG, aHeaderValue);
}

void HttpDownload::sendHeader(const char* aHeaderName, const int aHeaderValue)
{
  char buffer[16];
  itoa(aHeaderValue, buffer, 10);
  sendHeader(aHeaderName, buffer);
}

void HttpDownload::sendHeader(const char* aHeaderName)
{
  out(aHeaderName);

  logLine(LOGGING_LEVEL_DEBUG, aHeaderName);
}

void HttpDownload::log(int loggingLevel, const char* message) {
  if (mLoggingLevel >= loggingLevel) {
    Serial.print(message);
  }
}

void HttpDownload::log(int loggingLevel, const int message) {
  if (mLoggingLevel >= loggingLevel) {
    char itoaBuffer[16];
    itoa(message, itoaBuffer, 10);
    Serial.print(itoaBuffer);
  }
}

void HttpDownload::logLine(int loggingLevel, const char* message) {
  log(loggingLevel, message);
  log(loggingLevel, "\r\n");
}

void HttpDownload::logLine(int loggingLevel, const int message) {
  log(loggingLevel, message);
  log(loggingLevel, "\r\n");
}
