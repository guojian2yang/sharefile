/*
 * config_file_reader.h
 *
 *  Created on: 2013-7-2
 *      Author: ziteng@mogujie.com
 */

#ifndef CONFIGFILEREADER_H_
#define CONFIGFILEREADER_H_

#include "util.h"

class CConfigFileReader {
  public:
    CConfigFileReader(const char *filename);
    ~CConfigFileReader();

    char *GetConfigName(const char *name);
    int SetConfigValue(const char *name, const char *value);
    uint16_t get_http_bind_port() {return http_bind_port;}
  private:
    void _LoadFile(const char *filename);
    int _WriteFIle(const char *filename = NULL);
    void _ParseLine(char *line);
    char *_TrimSpace(char *name);

    bool load_ok_;
    map<string, string> config_map_; // key -value
    string config_file_;

    int16_t http_bind_port = 8081;
    string  http_bind_ip;
};

#endif /* CONFIGFILEREADER_H_ */
