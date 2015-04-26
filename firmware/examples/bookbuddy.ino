#include "SD.h"
#include "HttpDownload.h"

void setup() {
  Serial.begin(19200);
}

void loop() {

  simpleExample();
  complexExample();
}

void simpleExample() {
  HttpDownload http;

  HttpDownloadRequest request;
  HttpDownloadResponse response;

  request.hostname = "upload.wikimedia.org";
  request.port = 80;
  request.path = "/wikipedia/commons/9/95/Burning_Yellow_Sunset.jpg";

  http.download(request, response, "sunset-simple.jpg");

}

void complexExample() {
  //Parameters are
  //Logging level: The level of information you want printed to the serial console.
  //bytes per chunk: The bytes per TCP packet requested from the server.  If this, plus the
  //                  overhead of the HTTP headers exceeds 1460, the transfer will slow to a crawl.
  //                  Use the debug logs to see your packet size, and adjust this accordingly to get
  //                  it below 1460.  This whole fiasco is caused by a problem with the CC3000 firmware
  //Retry attempts: How many times to retry each failed packet before giving up on the whole transfer
  //Retry timeout:  How long to wait between retries.
  HttpDownload http(LOGGING_LEVEL_DEBUG, 1000, 5, 500);

  // Headers currently need to be set at init, useful for API keys etc.
  HttpDownloadHeader headers[] = {
    { "Accept" , "*/*"},
    { NULL, NULL } // NOTE: Always terminate headers will NULL
  };

  HttpDownloadRequest request;
  HttpDownloadResponse response;

  request.hostname = "upload.wikimedia.org";
  request.port = 80;
  request.path = "/wikipedia/commons/9/95/Burning_Yellow_Sunset.jpg";

  //We'll control writing each byte to a file we open.
  //Open a file.
  File outFile;
  char* outFileName = "sunset.jpg";
  if (SD.exists(outFileName)) {
    //delete it.
    SD.remove(outFileName);
  }
  outFile = SD.open(outFileName, FILE_WRITE);

  //Each chunk of data we receive will result in writeToFile being called.
  http.download(request, response, headers, writeToFile, &outFile);

  //Close the file.
  outFile.close();

  Serial.print("Response status: ");
  Serial.println(response.status);

}

//byte* fromResponse is the chunk of bytes we received from the server.
//int size is the size of that chunk.
//void* file is the object we passed in to the download method.
void writeToFile(byte* fromResponse, int size, void* file) {
  File* outFile = (File*)file;
  outFile->write(fromResponse, size);
}
