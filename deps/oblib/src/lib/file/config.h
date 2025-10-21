/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TBSYS_CONFIG_H
#define TBSYS_CONFIG_H

#include <string>
#include <ext/hash_map>
#include "lib/file/ob_string_util.h"


namespace obsys {
/**
* @brief Generate the hash value of string
*/
    struct st_str_hash {
        size_t operator()(const std::string& str) const {
            return __gnu_cxx::__stl_hash_string(str.c_str());
        }
    };
    typedef __gnu_cxx::hash_map<std::string, std::string, st_str_hash> STR_STR_MAP;
    typedef STR_STR_MAP::iterator STR_STR_MAP_ITER;
    typedef __gnu_cxx::hash_map<std::string, STR_STR_MAP*, st_str_hash> STR_MAP;
    typedef STR_MAP::iterator STR_MAP_ITER;

    #define OB_TBSYS_CONFIG obsys::ObSysConfig::getCConfig()

    /**
     * @brief Parse the configuration file, and store the configuration items as key-value pairs in memory
     */
    class ObSysConfig
    {
    public:
        ObSysConfig();
        ~ObSysConfig();

            // Load a file
            int load(const char *filename);
            // Load a buffer
            int loadContent(const char * content);
            // Take a string
            const char *getString(const char *section, const std::string& key, const char *d = NULL);
            // Take a list of strings
            std::vector<const char*> getStringList(const char *section, const std::string& key);
            // Take an integer
            int getInt(char const *section, const std::string& key, int d = 0);
            // Take an integer list
            std::vector<int> getIntList(const char *section, const std::string& key);
            // Take all the keys under a section
            int getSectionKey(const char *section, std::vector<std::string> &keys);
            // Get the names of all sections
            int getSectionName(std::vector<std::string> &sections);
            // Complete configuration file string
            std::string toString();
            // Get static instance
            static ObSysConfig& getCConfig();

        private:
            // Two-layer map
            STR_MAP m_configMap;

        private:
            // Parse string
            int parseValue(char *str, char *key, char *val);
            int parseLine(STR_STR_MAP *&m, char *data);
            int getLine(char * buf, const int buf_len,
                const char * content, const int content_len, int & pos);
            char *isSectionName(char *str);
    };
}

#endif
