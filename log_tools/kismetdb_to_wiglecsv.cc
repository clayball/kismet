/*
    This file is part of Kismet

    Kismet is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Kismet is distributed in the hope that it will be useful,
      but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Kismet; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* 
 * Simple tool to clean up a kismetdb log file, duplicate it, and strip the packet
 * content, in preparation to uploading to a site like wigle.
 */

#include "config.h"

#include <map>
#include <iomanip>
#include <ctime>
#include <iostream>

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <sqlite3.h>

#include "getopt.h"

#include "json/json.h"
#include "sqlite3_cpp11.h"

#include "packet_ieee80211.h"

// Aggressive additional mangle of text to handle converting ',' and '"' to
// hexcode for CSV
std::string MungeForCSV(const std::string& in_data) {
	std::string ret;

	for (size_t i = 0; i < in_data.length(); i++) {
		if ((unsigned char) in_data[i] >= 32 && (unsigned char) in_data[i] <= 126 &&
				in_data[i] != ',' && in_data[i] != '\"' ) {
			ret += in_data[i];
		} else {
			ret += '\\';
			ret += ((in_data[i] >> 6) & 0x03) + '0';
			ret += ((in_data[i] >> 3) & 0x07) + '0';
			ret += ((in_data[i] >> 0) & 0x07) + '0';
		}
	}

	return ret;
}

// Some conversion functions from Kismet for Wi-Fi channels
int FrequencyToWifiChannel(double in_freq) {
    if (in_freq == 0)
        return 0;

    in_freq = in_freq / 1000;

    if (in_freq == 2484)
        return 14;
    else if (in_freq < 2484)
        return (in_freq - 2407) / 5;
    else if (in_freq >= 4910 && in_freq <= 4980)
        return (in_freq - 4000) / 5;
    else if (in_freq <= 45000)
        return (in_freq - 5000) / 5;
    else if (in_freq >= 58320 && in_freq <= 64800)
        return (in_freq - 56160) / 2160;
    else
        return in_freq;
}

std::string WifiCryptToString(unsigned long cryptset) {
    std::stringstream ss;

    if (cryptset & crypt_wps)
        ss << "[WPS] ";

    if ((cryptset & crypt_protectmask) == crypt_wep) 
        ss << "[WEP] ";

    if (cryptset & crypt_wpa) {

        std::string cryptver = "";

        if (cryptset & crypt_tkip) {
            if (cryptset & crypt_aes_ccm) {
                cryptver = "CCMP+TKIP";
            } else {
                cryptver = "TKIP";
            }
        } else if (cryptset & crypt_aes_ccm) {
            cryptver = "CCMP";
        }

        std::string authver = "";

        if (cryptset & crypt_psk) {
            authver = "PSK";
        } else if (cryptset & crypt_eap) {
            authver = "EAP";
        }

        if ((cryptset & crypt_version_wpa) && (cryptset & crypt_version_wpa2)) {
            ss << "[WPA-" << authver << "-" << cryptver << "] ";
            ss << "[WPA2-" << authver << "-" << cryptver << "] ";
        } else if (cryptset & crypt_version_wpa2) {
            ss << "[WPA2-" << authver << "-" << cryptver << "] ";
        } else {
            ss << "[WPA-" << authver << "-" << cryptver << "] ";
        }
    }

    auto retstr = ss.str();

    if (retstr.length() > 0)
        return retstr.substr(0, retstr.length() - 1);

    return "";
}

void print_help(char *argv) {
    printf("Kismetdb to WigleCSV\n");
    printf("A simple tool for converting the packet data from a KismetDB log file to\n"
           "the CSV format used by Wigle\n");
    printf("Usage: %s [OPTION]\n", argv);
    printf(" -i, --in [filename]          Input kismetdb file\n"
           " -o, --out [filename]         Output Wigle CSV file\n"
           " -f, --force                  Force writing to the target file, even if it exists.\n"
           " -r, --rate-limit [rate]      Limit updated records to one update per [rate] seconds\n"
           "                              per device\n"
           " -c, --cache-limit [limit]    Maximum number of device to cache, defaults to 1000.\n"
           " -v, --verbose                Verbose output\n"
           " -s, --skip-clean             Don't clean (sql vacuum) input database\n");
}

int main(int argc, char *argv[]) {
    static struct option longopt[] = {
        { "in", required_argument, 0, 'i' },
        { "out", required_argument, 0, 'o' },
        { "verbose", no_argument, 0, 'v' },
        { "force", no_argument, 0, 'f' },
        { "help", no_argument, 0, 'h' },
        { "skip-clean", no_argument, 0, 's' },
        { "rate-limit", required_argument, 0, 'r'},
        { "cache-limit", required_argument, 0, 'c'},
        { 0, 0, 0, 0 }
    };

    int option_idx = 0;
    optind = 0;
    opterr = 0;

    std::string in_fname, out_fname;
    bool verbose = false;
    bool force = false;
    bool skipclean = false;

    int sql_r = 0;
    char *sql_errmsg = NULL;
    sqlite3 *db = NULL;

    FILE *ofile = NULL;

    struct stat statbuf;

    unsigned int rate_limit = 0;
    unsigned int cache_limit = 1000;

    while (1) {
        int r = getopt_long(argc, argv, 
                            "-hi:o:r:c:vfs", 
                            longopt, &option_idx);
        if (r < 0) break;

        if (r == 'h') {
            print_help(argv[0]);
            exit(1);
        } else if (r == 'i') {
            in_fname = std::string(optarg);
        } else if (r == 'o') {
            out_fname = std::string(optarg);
        } else if (r == 'v') { 
            verbose = true;
        } else if (r == 'f') {
            force = true;
        } else if (r == 's') {
            skipclean = true;
        } else if (r == 'r') {
            if (sscanf(optarg, "%u", &rate_limit) != 1) {
                fprintf(stderr, "ERROR:  Expected a rate limit of # seconds between packets of the same device.\n");
                exit(1);
            }
        } else if (r == 'c') {
            if (sscanf(optarg, "%u", &cache_limit) != 1) {
                fprintf(stderr, "ERROR:  Expected a cache limit number.\n");
                exit(1);
            }
        }
    }

    if (out_fname == "" || in_fname == "") {
        fprintf(stderr, "ERROR: Expected --in [kismetdb file] and "
                "--out [wigle CSV file]\n");
        exit(1);
    }

    if (stat(in_fname.c_str(), &statbuf) < 0) {
        if (errno == ENOENT) 
            fprintf(stderr, "ERROR:  Input file '%s' does not exist.\n", 
                    in_fname.c_str());
        else
            fprintf(stderr, "ERROR:  Unexpected problem checking input "
                    "file '%s': %s\n", in_fname.c_str(), strerror(errno));

        exit(1);
    }

    if (out_fname != "-") {
        if (stat(out_fname.c_str(), &statbuf) < 0) {
            if (errno != ENOENT) {
                fprintf(stderr, "ERROR:  Unexpected problem checking output "
                        "file '%s': %s\n", out_fname.c_str(), strerror(errno));
                exit(1);
            }
        } else if (force == false) {
            fprintf(stderr, "ERROR:  Output file '%s' exists already; use --force to "
                    "clobber the file.\n", out_fname.c_str());
            exit(1);
        }
    }

    /* Open the database and run the vacuum command to clean up any stray journals */
    sql_r = sqlite3_open(in_fname.c_str(), &db);

    if (sql_r) {
        fprintf(stderr, "ERROR:  Unable to open '%s': %s\n", in_fname.c_str(), sqlite3_errmsg(db));
        exit(1);
    }

    if (!skipclean) {
        if (verbose)
            fprintf(stderr, "* Preparing input database '%s'...\n", in_fname.c_str());


        sql_r = sqlite3_exec(db, "VACUUM;", NULL, NULL, &sql_errmsg);

        if (sql_r != SQLITE_OK) {
            fprintf(stderr, "ERROR:  Unable to clean up (vacuum) database before copying: %s\n",
                    sql_errmsg);
            sqlite3_close(db);
            exit(1);
        }
    }

    // Use our sql adapters

    using namespace kissqlite3;

    int db_version = 0;
    long int n_total_packets_db = 0L;
    long int n_packets_db = 0L;
    long int n_devices_db = 0L;

    try {
        // Get the version
        auto version_query = _SELECT(db, "KISMET", {"db_version"});
        auto version_ret = version_query.begin();
        if (version_ret == version_query.end()) {
            fprintf(stderr, "ERROR:  Unable to fetch database version.\n");
            sqlite3_close(db);
            exit(1);
        }
        db_version = sqlite3_column_as<int>(*version_ret, 0);

        if (verbose)
            fprintf(stderr, "* Found KismetDB version %d\n", db_version);

        // Get the total counts
        auto npackets_q = _SELECT(db, "packets", 
                {"count(*), sum(case when (sourcemac != '00:00:00:00:00:00' "
                "and lat != 0 and lon != 0) then 1 else 0 end)"});
        auto npackets_ret = npackets_q.begin();
        if (npackets_ret == npackets_q.end()) {
            fprintf(stderr, "ERROR:  Unable to fetch packet count.\n");
            sqlite3_close(db);
            exit(1);
        }
        n_total_packets_db = sqlite3_column_as<unsigned long>(*npackets_ret, 0);
        n_packets_db = sqlite3_column_as<unsigned long>(*npackets_ret, 1);

        auto ndevices_q = _SELECT(db, "devices", {"count(*)"});
        auto ndevices_ret = ndevices_q.begin();
        if (ndevices_ret == ndevices_q.end()) {
            fprintf(stderr, "ERROR:  Unable to fetch device count.\n");
            sqlite3_close(db);
            exit(1);
        }
        n_devices_db = sqlite3_column_as<unsigned long>(*ndevices_ret, 0);

        if (verbose) 
            fprintf(stderr, "* Found %lu devices, %lu usable packets, %lu total packets\n", 
                    n_devices_db, n_packets_db, n_total_packets_db);

        if (n_packets_db == 0) {
            fprintf(stderr, "ERROR:  No usable packets in the log file; packets must have GPS information\n"
                            "        to be usable with Wigle.\n");
            sqlite3_close(db);
            exit(1);
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "ERROR:  Could not get database information from '%s': %s\n",
                in_fname.c_str(), e.what());
        exit(0);
    }

    if (out_fname == "-") {
        ofile = stdout;
    } else {
        ofile = fopen(out_fname.c_str(), "w");
        if (ofile == NULL) {
            fprintf(stderr, "ERROR:  Unable to open output file for writing: %s\n",
                    strerror(errno));
            exit(1);
        }
    }

    // Define a simple cache; we don't need to use a proper kismet macaddr here, just operate
    // on it as a string
    class cache_obj {
    public:
        cache_obj(std::string t, std::string s, std::string c) :
            first_time{t},
            name{s},
            crypto{c},
            last_time_sec{0} { }

        std::string first_time;
        std::string name;
        std::string crypto;
        uint64_t last_time_sec;
    };

    std::map<std::string, cache_obj *> device_cache_map;

    if (verbose) 
        fprintf(stderr, "* Starting to process file, max device cache %u\n", cache_limit);

    // CSV headers
    fprintf(ofile, "WigleWifi-1.4,appRelease=20190201,model=Kismet,release=2019.02.01.%d,"
            "device=kismet,display=kismet,board=kismet,brand=kismet\n",
            db_version);
    fprintf(ofile, "MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,CurrentLongitude,"
            "AltitudeMeters,AccuracyMeters,Type\n");

    // Prep the packet list for different kismetdb versions
    std::list<std::string> packet_fields;

    if (db_version < 5) {
        packet_fields = std::list<std::string>{"ts_sec", "sourcemac", "phyname", "lat", "lon", "signal", "frequency"};
    } else {
        packet_fields = std::list<std::string>{"ts_sec", "sourcemac", "phyname", "lat", "lon", "signal", "frequency", "alt", "speed"};
    }

    auto query = _SELECT(db, "packets", packet_fields,
            _WHERE("sourcemac", NEQ, "00:00:00:00:00:00", 
                AND, 
                "lat", NEQ, 0,
                AND,
                "lon", NEQ, 0));

    unsigned long n_logs = 0;
    unsigned long n_discarded_logs = 0;
    unsigned long n_division = (n_packets_db / 20);

    for (auto p : query) {
        // Brute-force cache maintenance; if we're full at the start of the 
        // processing loop, nuke the ENTIRE cache and rebuild it; this is
        // cleaner than constantly re-sorting it.
        if (device_cache_map.size() >= cache_limit) {
            if (verbose)
                fprintf(stderr, "* Cleaning cache...\n");

            for (auto i : device_cache_map) {
                delete(i.second);
            }

            device_cache_map.clear();
        }

        n_logs++;
        if (n_logs % n_division == 0 && verbose)
            fprintf(stderr, "* %d%% Processed %lu records, %lu discarded from rate limiting, cache %lu\n", 
                    (int) (((float) n_logs / (float) n_packets_db) * 100) + 1, 
                    n_logs, n_discarded_logs, device_cache_map.size());

        auto ts = sqlite3_column_as<std::uint64_t>(p, 0);
        auto sourcemac = sqlite3_column_as<std::string>(p, 1);
        auto phy = sqlite3_column_as<std::string>(p, 2);

        auto lat = 0.0f, lon = 0.0f, alt = 0.0f, spd = 0.0f;

        auto signal = sqlite3_column_as<int>(p, 5);
        auto channel = sqlite3_column_as<double>(p, 6);

        auto crypt = std::string{""};

        // Handle the different versions
        if (db_version < 5) {
            lat = sqlite3_column_as<double>(p, 3) / 100000;
            lon = sqlite3_column_as<double>(p, 4) / 100000;
        } else {
            lat = sqlite3_column_as<double>(p, 3);
            lon = sqlite3_column_as<double>(p, 4);
            alt = sqlite3_column_as<double>(p, 7);
            spd = sqlite3_column_as<double>(p, 8);
        }

        auto ci = device_cache_map.find(sourcemac);
        cache_obj *cached = nullptr;

        if (ci != device_cache_map.end()) {
            cached = ci->second;
        } else {
            auto dev_query = _SELECT(db, "devices", {"device"},
                    _WHERE("devmac", EQ, sourcemac,
                        AND,
                        "phyname", EQ, phy));

            auto dev = dev_query.begin();

            if (dev == dev_query.end()) {
                // printf("Could not find device record for %s\n", sqlite3_column_as<std::string>(p, 0).c_str());
                continue;
            }

            Json::Value json;
            std::stringstream ss(sqlite3_column_as<std::string>(*dev, 0));

            try {
                ss >> json;

                auto timestamp = json["kismet.device.base.first_time"].asUInt64();
                auto name = std::string{""};
                auto crypt = std::string{""};
                auto type = json["kismet.device.base.type"].asString();

                if (phy == "IEEE802.11") {
                    if (type != "Wi-Fi AP")
                        continue;

                    name = MungeForCSV(json["dot11.device"]["dot11.device.last_beaconed_ssid"].asString());

                    auto last_ssid_key = 
                        json["dot11.device"]["dot11.device.last_beaconed_ssid_checksum"].asUInt64();
                    std::stringstream ss;

                    ss << last_ssid_key;

                    auto cryptset = 

                    crypt = WifiCryptToString(
                            json["dot11.device"]["dot11.device.advertised_ssid_map"][ss.str()]["dot11.advertisedssid.crypt_set"].asUInt64()
                            );

                    crypt += "[ESS]";

                }

                std::time_t timet(timestamp);
                std::tm tm = *std::localtime(&timet);
                std::stringstream ts;

                ts << std::put_time(&tm, "%Y-%b-%d %H:%M:%S"); 

                cached = new cache_obj{ts.str(), name, crypt};

                device_cache_map[sourcemac] = cached;

            } catch (const std::exception& e) {
                fprintf(stderr, "WARNING:  Could not process device info for %s/%s, skipping\n",
                        sourcemac.c_str(), phy.c_str());
            }
        }

        if (cached == nullptr)
            continue;

        // Rate throttle
        if (rate_limit != 0 && cached->last_time_sec != 0) {
            if (cached->last_time_sec + rate_limit < ts) {
                n_discarded_logs++;
                continue;
            }
        } 
        cached->last_time_sec = ts;

        if (phy == "IEEE802.11")
            channel = FrequencyToWifiChannel(channel);

        // printf("MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type\n");

        fprintf(ofile, "%s,%s,%s,%s,%d,%d,%lf,%f,%lf,0,%s\n",
                sourcemac.c_str(),
                cached->name.c_str(),
                cached->crypto.c_str(),
                cached->first_time.c_str(),
                (int) channel,
                signal,
                lat, lon, alt,
                "WIFI");
    }

    if (ofile != stdout)
        fclose(ofile);

    sqlite3_close(db);

    if (verbose)  {
        fprintf(stderr, "* Processed %lu records, %lu discarded from rate limiting, %lu devices\n", 
                n_logs, n_discarded_logs, device_cache_map.size());
        fprintf(stderr, "* Done!\n");
    }

    return 0;
}

