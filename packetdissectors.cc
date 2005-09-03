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

#include "config.h"

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif

#include <map>
#include <iomanip>
#include <sstream>

#include "endian_magic.h"
#include "packetdissectors.h"
#include "packetsignatures.h"
#include "packetchain.h"
#include "alertracker.h"
#include "configfile.h"

#define PROTO_SSID_LEN		32

// Handly little global so that it only has to do the ascii->mac_addr transform once
mac_addr broadcast_mac = "FF:FF:FF:FF:FF:FF";

char *WEPKEY_fields_text[] = {
    "origin", "bssid", "key", "encrypted", "failed",
    NULL
};

int proto_WEPKEY(PROTO_PARMS) {
	wep_key_info *winfo = (wep_key_info *) data;
	ostringstream osstr;

	// We don't use the cache
	
	for (unsigned int x = 0; x < field_vec->size(); x++) {
		unsigned int fnum = (*field_vec)[x];

		osstr.str("");

		switch (fnum) {
			case WEPKEY_origin:
				if (winfo->fragile == 0)
					out_string += "0";
				else
					out_string += "1";
				break;
			case WEPKEY_bssid:
				out_string += winfo->bssid.Mac2String();
				break;
			case WEPKEY_key:
				for (unsigned int kpos = 0; kpos < WEPKEY_MAX && 
					 kpos < winfo->len; kpos++) {
					osstr << setprecision(2) << hex << winfo->key[kpos];
					if (kpos < (WEPKEY_MAX - 1) && kpos < (winfo->len - 1))
						osstr << ":";
					out_string += osstr.str();
				}
				break;
			case WEPKEY_decrypted:
				osstr << winfo->decrypted;
				out_string += osstr.str();
				break;
			case WEPKEY_failed:
				osstr << winfo->failed;
				out_string += osstr.str();
				break;
			default:
				out_string = "\001Unknown field requested\001";
				return -1;
				break;
		}

		out_string += " ";
	}

	return 1;
}

int clicmd_LISTWEPKEYS_hook(CLIENT_PARMS) {
	KisBuiltinDissector *di = (KisBuiltinDissector *) auxptr;
	return di->cmd_listwepkeys(in_clid, framework, globalreg, errstr, cmdline,
							   parsedcmdline, auxptr);
}

int clicmd_ADDWEPKEY_hook(CLIENT_PARMS) {
	KisBuiltinDissector *di = (KisBuiltinDissector *) auxptr;
	return di->cmd_addwepkey(in_clid, framework, globalreg, errstr, cmdline,
							   parsedcmdline, auxptr);
}

int clicmd_DELWEPKEY_hook(CLIENT_PARMS) {
	KisBuiltinDissector *di = (KisBuiltinDissector *) auxptr;
	return di->cmd_delwepkey(in_clid, framework, globalreg, errstr, cmdline,
							   parsedcmdline, auxptr);
}


// CRC32 index for verifying WEP - cribbed from ethereal
static const uint32_t wep_crc32_table[256] = {
    0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
    0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
    0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
    0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
    0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
    0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
    0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
    0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
    0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
    0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
    0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
    0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
    0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
    0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
    0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
    0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
    0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
    0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
    0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
    0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
    0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
    0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
    0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
    0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
    0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
    0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
    0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
    0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
    0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
    0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
    0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
    0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
    0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
    0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
    0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
    0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
    0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
    0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
    0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
    0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
    0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
    0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
    0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
    0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
    0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
    0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
    0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
    0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
    0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
    0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
    0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
    0x2d02ef8dL
};

// Hooks into the real functions
int kis_80211_dissector(CHAINCALL_PARMS) {
	KisBuiltinDissector *auxptr = (KisBuiltinDissector *) auxdata;
	return auxptr->ieee80211_dissector(in_pack);
}

int kis_data_dissector(CHAINCALL_PARMS) {
	KisBuiltinDissector *auxptr = (KisBuiltinDissector *) auxdata;
	return auxptr->basicdata_dissector(in_pack);
}

int kis_wep_decryptor(CHAINCALL_PARMS) {
	KisBuiltinDissector *auxptr = (KisBuiltinDissector *) auxdata;
	return auxptr->wep_data_decryptor(in_pack);
}

KisBuiltinDissector::KisBuiltinDissector() {
	fprintf(stderr, "FATAL OOPS:  KisBuiltinDissector called with no globalreg\n");
	exit(1);
}

KisBuiltinDissector::KisBuiltinDissector(GlobalRegistry *in_globalreg) {
	globalreg = in_globalreg;
	char errstr[STATUS_MAX];

	if (globalreg->packetchain == NULL) {
		fprintf(stderr, "FATAL OOPS:  KisBuiltinDissector called before "
				"packetchain\n");
		exit(1);
	}

	if (globalreg->alertracker == NULL) {
		fprintf(stderr, "FATAL OOPS:  KisBuiltinDissector called before "
				"alertracker\n");
		exit(1);
	}

	if (globalreg->kisnetserver == NULL) {
		fprintf(stderr, "FATAL OOPS:  KisBuiltinDissector called before "
				"kisnetserver\n");
		exit(1);
	}

	// Register the basic stuff
	globalreg->packetchain->RegisterHandler(&kis_wep_decryptor, this,
											CHAINPOS_DECRYPT, -100);
	globalreg->packetchain->RegisterHandler(&kis_80211_dissector, this,
											CHAINPOS_LLCDISSECT, -100);
	globalreg->packetchain->RegisterHandler(&kis_data_dissector, this,
											CHAINPOS_DATADISSECT, -100);

	_PCM(PACK_COMP_80211) = 
		globalreg->packetchain->RegisterPacketComponent("IEEE80211_INFO");

	_PCM(PACK_COMP_BASICDATA) = 
		globalreg->packetchain->RegisterPacketComponent("BASICDATA_INFO");

	_PCM(PACK_COMP_MANGLEFRAME) =
		globalreg->packetchain->RegisterPacketComponent("MANGLE_FRAME");

	netstumbler_aref = 
		globalreg->alertracker->ActivateConfiguredAlert("NETSTUMBLER");
	nullproberesp_aref =
		globalreg->alertracker->ActivateConfiguredAlert("NULLPROBERESP");
	lucenttest_aref =
		globalreg->alertracker->ActivateConfiguredAlert("LUCENTTEST");

	// Register network protocols for WEP key transfer commands
	_NPM(PROTO_REF_WEPKEY) =
		globalreg->kisnetserver->RegisterProtocol("WEPKEY", 0, 0, WEPKEY_fields_text,
												  &proto_WEPKEY, NULL);
	globalreg->kisnetserver->RegisterClientCommand("LISTWEPKEYS", 
												   clicmd_LISTWEPKEYS_hook,
												   this);
	globalreg->kisnetserver->RegisterClientCommand("ADDWEPKEY", 
												   clicmd_ADDWEPKEY_hook,
												   this);
	globalreg->kisnetserver->RegisterClientCommand("DELWEPKEY", 
												   clicmd_DELWEPKEY_hook,
												   this);

    // Convert the WEP mappings to our real map
    vector<string> raw_wepmap_vec;
    raw_wepmap_vec = globalreg->kismet_config->FetchOptVec("wepkey");
    for (size_t rwvi = 0; rwvi < raw_wepmap_vec.size(); rwvi++) {
        string wepline = raw_wepmap_vec[rwvi];

        size_t rwsplit = wepline.find(",");
        if (rwsplit == string::npos) {
            _MSG("Malformed 'wepkey' option in the config file", MSGFLAG_FATAL);
			globalreg->fatal_condition = 1;
			return;
        }

        mac_addr bssid_mac = wepline.substr(0, rwsplit).c_str();

        if (bssid_mac.error == 1) {
            _MSG("Malformed 'wepkey' option in the config file", MSGFLAG_FATAL);
			globalreg->fatal_condition = 1;
			return;
        }

        string rawkey = wepline.substr(rwsplit + 1, wepline.length() - (rwsplit + 1));

        unsigned char key[WEPKEY_MAX];
        int len = Hex2UChar((unsigned char *) rawkey.c_str(), key);

        if (len != 5 && len != 13 && len != 16) {
            snprintf(errstr, STATUS_MAX, "Invalid key '%s' length %d in a wepkey "
					 "option in the config file.\n", rawkey.c_str(), len);
			_MSG(errstr, MSGFLAG_FATAL);
			globalreg->fatal_condition = 1;
			return;
        }

        wep_key_info *keyinfo = new wep_key_info;
        keyinfo->bssid = bssid_mac;
        keyinfo->fragile = 0;
        keyinfo->decrypted = 0;
        keyinfo->failed = 0;
        keyinfo->len = len;
        memcpy(keyinfo->key, key, sizeof(unsigned char) * WEPKEY_MAX);

        wepkeys.insert(bssid_mac, keyinfo);

        snprintf(errstr, STATUS_MAX, "Using key %s length %d for BSSID %s",
                rawkey.c_str(), len, bssid_mac.Mac2String().c_str());
        _MSG(errstr, MSGFLAG_INFO);
    }

    if (globalreg->kismet_config->FetchOpt("allowkeytransmit") == "true") {
        _MSG("Allowing Kismet frontends to view WEP keys", MSGFLAG_INFO);
        client_wepkey_allowed = 1;
    } else {
		client_wepkey_allowed = 0;
	}

	// Build the wep identity
	for (unsigned int wi = 0; wi < 256; wi++)
		wep_identity[wi] = wi;
}

// Returns a pointer in the data block to the size byte of the desired tag, with the 
// tag offsets cached
int KisBuiltinDissector::GetIEEETagOffsets(unsigned int init_offset, 
										kis_datachunk *in_chunk,
										map<int, vector<int> > *tag_cache_map) {
    int cur_tag = 0;
    // Initial offset is 36, that's the first tag
    unsigned int cur_offset = init_offset;
    uint8_t len;

    // Bail on invalid incoming offsets
    if (init_offset >= (uint8_t) in_chunk->length)
        return -1;
    
    // If we haven't parsed the tags for this frame before, parse them all now.
    // Return an error code if one of them is malformed.
    if (tag_cache_map->size() == 0) {
        while (1) {
            // Are we over the packet length?
            if (cur_offset >= (uint8_t) in_chunk->length) {
                break;
            }

            // Read the tag we're on and bail out if we're done
            cur_tag = (int) in_chunk->data[cur_offset];

            // Move ahead one byte and read the length.
            len = (in_chunk->data[cur_offset+1] & 0xFF);

            // If this is longer than we have...
            if ((cur_offset + len + 2) > in_chunk->length) {
                return -1;
            }

            // (*tag_cache_map)[cur_tag] = cur_offset + 1;
			
            (*tag_cache_map)[cur_tag].push_back(cur_offset + 1);

            // Jump the length+length byte, this should put us at the next tag
            // number.
            cur_offset += len+2;
        }
    }
    
    return 0;
}

// Convert WPA cipher elements into crypt_set stuff
int KisBuiltinDissector::WPACipherConv(uint8_t cipher_index) {
	int ret = crypt_wpa;

	switch (cipher_index) {
		case 1:
			ret |= crypt_wep40;
			break;
		case 2:
			ret |= crypt_tkip;
			break;
		case 3:
			ret |= crypt_aes_ocb;
			break;
		case 4:
			ret |= crypt_aes_ccm;
			break;
		case 5:
			ret |= crypt_wep104;
			break;
		default:
			ret = 0;
			break;
	}

	return ret;
}

// Convert WPA key management elements into crypt_set stuff
int KisBuiltinDissector::WPAKeyMgtConv(uint8_t mgt_index) {
	int ret = crypt_wpa;

	switch (mgt_index) {
		case 1:
			ret |= crypt_wpa;
			break;
		case 2:
			ret |= crypt_psk;
			break;
		default:
			ret = 0;
			break;
	}

	return ret;
}

// This needs to be optimized and it needs to not use casting to do its magic
int KisBuiltinDissector::ieee80211_dissector(kis_packet *in_pack) {
	static int debugpcknum = 0;

	if (in_pack->error)
		return 0;

	debugpcknum++;
	// printf("debug - packet %d\n", debugpcknum);

    // Extract data, bail if it doesn't exist, make a local copy of what we're
    // inserting into the frame.
    kis_ieee80211_packinfo *packinfo;
	kis_datachunk *chunk = 
		(kis_datachunk *) in_pack->fetch(_PCM(PACK_COMP_80211FRAME));

	// If we can't grab an 802.11 chunk, grab the raw link frame
	if (chunk == NULL) {
		chunk = (kis_datachunk *) in_pack->fetch(_PCM(PACK_COMP_LINKFRAME));
		if (chunk == NULL) {
			return 0;
		}
	}

	// Flat-out dump if it's not big enough to be 80211.
    if (chunk->length < 24)
        return 0;

    packinfo = new kis_ieee80211_packinfo;

    frame_control *fc = (frame_control *) chunk->data;

    uint16_t duration = 0;

    // 18 bytes of normal address ranges
    uint8_t *addr0;
    uint8_t *addr1;
    uint8_t *addr2;
    // And an optional 6 bytes of address range for ds=0x03 packets
    uint8_t *addr3;

    // We'll fill these in as we go
    packinfo->type = packet_unknown;
    packinfo->subtype = packet_sub_unknown;
    packinfo->distrib = distrib_unknown;

    // Endian swap the duration  ** Optimize this in the future **
    memcpy(&duration, &(chunk->data[2]), 2);
    duration = kis_ntoh16(duration);

    // 2 bytes of sequence and fragment counts
    wireless_fragseq *sequence;

    addr0 = &(chunk->data[4]);
    addr1 = &(chunk->data[10]);
    addr2 = &(chunk->data[16]);
    sequence = (wireless_fragseq *) &(chunk->data[22]);
    addr3 = &(chunk->data[24]);

    packinfo->sequence_number = sequence->sequence;
    packinfo->frag_number = sequence->frag;

	if (fc->more_fragments)
		packinfo->fragmented = 1;

    unsigned int tag_offset = 0;
	unsigned int taglen = 0;

    // Assign the distribution direction this packet is traveling
    if (fc->to_ds == 0 && fc->from_ds == 0)
        packinfo->distrib = distrib_adhoc; 
    else if (fc->to_ds == 0 && fc->from_ds == 1)
        packinfo->distrib = distrib_from;
    else if (fc->to_ds == 1 && fc->from_ds == 0)
        packinfo->distrib = distrib_to;
    else if (fc->to_ds == 1 && fc->from_ds == 1)
        packinfo->distrib = distrib_inter;

    // Rip apart management frames
    if (fc->type == 0) {
        packinfo->type = packet_management;

        packinfo->distrib = distrib_unknown;

        // Throw away large management frames that don't make any sense.  512b is 
        // an arbitrary number to pick, but this should keep some drivers from messing
        // with us
        if (chunk->length > 512) {
            packinfo->corrupt = 1;
            in_pack->insert(_PCM(PACK_COMP_80211), packinfo);
            return 0;
        }

        fixed_parameters *fixparm;

        if (fc->subtype == 4 || fc->subtype == 10 || fc->subtype == 11 || 
			fc->subtype == 12) {
			// Shortcut handling of probe req, disassoc, auth, deauth since they're
			// not normal management frames
            packinfo->header_offset = 24;
            fixparm = NULL;
        } else {
			// If we're not long enough to have the fixparm and look like a normal
			// mgt header, bail.
			if (chunk->length < 36) {
				packinfo->corrupt = 1;
				in_pack->insert(_PCM(PACK_COMP_80211), packinfo);
				return 0;
			}
            packinfo->header_offset = 36;
            fixparm = (fixed_parameters *) &(chunk->data[24]);
			if (fixparm->wep)
				packinfo->cryptset |= crypt_wep;

            // Pull the fixparm ibss info
            if (fixparm->ess == 0 && fixparm->ibss == 1) {
                packinfo->distrib = distrib_adhoc;
            }

            // Pull the fixparm timestamp
            uint64_t temp_ts;
            memcpy(&temp_ts, fixparm->timestamp, 8);
#ifdef WORDS_BIGENDIAN
            packinfo = kis_swap64(temp_ts);
#else
            packinfo->timestamp = temp_ts;
#endif
        }

        map<int, vector<int> > tag_cache_map;
        map<int, vector<int> >::iterator tcitr;

        // Extract various tags from the packet
        int found_ssid_tag = 0;
        int found_rate_tag = 0;
        int found_channel_tag = 0;

        if (fc->subtype == 8 || fc->subtype == 4 || fc->subtype == 5) {
            // This is guaranteed to only give us tags that fit within the packets,
            // so we don't have to do more error checking
            if (GetIEEETagOffsets(packinfo->header_offset, chunk, 
								  &tag_cache_map) < 0) {
                // The frame is corrupt, bail
                packinfo->corrupt = 1;
				in_pack->insert(_PCM(PACK_COMP_80211), packinfo);
                return 0;
            }
     
            if ((tcitr = tag_cache_map.find(0)) != tag_cache_map.end()) {
                tag_offset = tcitr->second[0];

                found_ssid_tag = 1;
                taglen = (chunk->data[tag_offset] & 0xFF);
                packinfo->ssid_len = taglen;

                // Protect against malicious packets
                if (taglen == 0) {
                    // do nothing for 0-length ssid's
                } else if (taglen <= PROTO_SSID_LEN) {
					// Test the SSID for cloaked len!=0 data==0 situation,
					// then munge it to something printable if it makes sense
					// to do so

					int zeroed = 1;
					for (unsigned int sp = 0; sp < taglen; sp++) {
						if (chunk->data[tag_offset+sp+1] != 0) {
							zeroed = 0;
							break;
						}
					}

					if (zeroed == 0) {
						packinfo->ssid = 
							MungeToPrintable((char *) 
											 &(chunk->data[tag_offset+1]), taglen);
					} else {
						packinfo->ssid_blank = 1;
					}
                } else {
                    // Otherwise we're corrupt, set it and stop processing
                    packinfo->corrupt = 1;
                    in_pack->insert(_PCM(PACK_COMP_80211), packinfo);
                    return 0;
                }
            } else {
                packinfo->ssid_len = 0;
            }

            // Extract the supported rates
            if ((tcitr = tag_cache_map.find(1)) != tag_cache_map.end()) {
                tag_offset = tcitr->second[0];
                taglen = (chunk->data[tag_offset] & 0xFF);

				if (tag_offset + taglen > chunk->length) {
                    // Otherwise we're corrupt, set it and stop processing
                    packinfo->corrupt = 1;
                    in_pack->insert(_PCM(PACK_COMP_80211), packinfo);
                    return 0;
				}

                found_rate_tag = 1;
                for (unsigned int x = 0; x < taglen; x++) {
                    if (packinfo->maxrate < 
						(chunk->data[tag_offset+1+x] & 0x7F) * 0.5)
                        packinfo->maxrate = 
							(chunk->data[tag_offset+1+x] & 0x7F) * 0.5;
                }
            }

			// And the extended supported rates
            if ((tcitr = tag_cache_map.find(50)) != tag_cache_map.end()) {
                tag_offset = tcitr->second[0];
                taglen = (chunk->data[tag_offset] & 0xFF);

				if (tag_offset + taglen > chunk->length) {
                    // Otherwise we're corrupt, set it and stop processing
                    packinfo->corrupt = 1;
                    in_pack->insert(_PCM(PACK_COMP_80211), packinfo);
                    return 0;
				}

                found_rate_tag = 1;
                for (unsigned int x = 0; x < taglen; x++) {
                    if (packinfo->maxrate < 
						(chunk->data[tag_offset+1+x] & 0x7F) * 0.5)
                        packinfo->maxrate = 
							(chunk->data[tag_offset+1+x] & 0x7F) * 0.5;
                }
            }

            // Find the offset of flag 3 and get the channel.   802.11a doesn't have 
            // this tag so we use the hardware channel, assigned at the beginning of 
            // GetPacketInfo
            if ((tcitr = tag_cache_map.find(3)) != tag_cache_map.end()) {
                tag_offset = tcitr->second[0];
                found_channel_tag = 1;
                // Extract the channel from the next byte (GetTagOffset returns
                // us on the size byte)
                taglen = (chunk->data[tag_offset] & 0xFF);

				if (tag_offset + taglen > chunk->length) {
                    // Otherwise we're corrupt, set it and stop processing
                    packinfo->corrupt = 1;
                    in_pack->insert(_PCM(PACK_COMP_80211), packinfo);
                    return 0;
				}
				
                packinfo->channel = (int) (chunk->data[tag_offset+1]);
            }
        }

        if (fc->subtype == 0) {
            packinfo->subtype = packet_sub_association_req;

            packinfo->dest_mac = addr0;
            packinfo->source_mac = addr1;
            packinfo->bssid_mac = addr2;

        } else if (fc->subtype == 1) {
            packinfo->subtype = packet_sub_association_resp;

            packinfo->dest_mac = addr0;
            packinfo->source_mac = addr1;
            packinfo->bssid_mac = addr2;

        } else if (fc->subtype == 2) {
            packinfo->subtype = packet_sub_reassociation_req;

            packinfo->dest_mac = addr0;
            packinfo->source_mac = addr1;
            packinfo->bssid_mac = addr2;

        } else if (fc->subtype == 3) {
            packinfo->subtype = packet_sub_reassociation_resp;

            packinfo->dest_mac = addr0;
            packinfo->source_mac = addr1;
            packinfo->bssid_mac = addr2;

        } else if (fc->subtype == 4) {
            packinfo->subtype = packet_sub_probe_req;

            packinfo->distrib = distrib_to;
            
            packinfo->source_mac = addr1;
            packinfo->bssid_mac = addr1;
           
            // Probe req's with no SSID are bad
            if (found_ssid_tag == 0) {
                packinfo->corrupt = 1;
                in_pack->insert(_PCM(PACK_COMP_80211), packinfo);
                return 0;
            }

        } else if (fc->subtype == 5) {
            packinfo->subtype = packet_sub_probe_resp;

            packinfo->dest_mac = addr0;
            packinfo->source_mac = addr1;
            packinfo->bssid_mac = addr2;

            /*
            if (ret_packinfo->ess == 0) {
                // A lot of cards seem to rotate through adhoc BSSID's, so we use 
				// the source instead
                ret_packinfo->bssid_mac = ret_packinfo->source_mac;
                ret_packinfo->distrib = adhoc_distribution;
                }
                */

        } else if (fc->subtype == 8) {
            packinfo->subtype = packet_sub_beacon;

            packinfo->beacon_interval = kis_ntoh16(fixparm->beacon);

            // Extract the CISCO beacon info
            if ((tcitr = tag_cache_map.find(133)) != tag_cache_map.end()) {
                tag_offset = tcitr->second[0];
                taglen = (chunk->data[tag_offset] & 0xFF);

				// Copy and munge the beacon info if it falls w/in our
				// boundaries
				if ((tag_offset + 11) < chunk->length && taglen >= 11) {
					packinfo->beacon_info = 
						MungeToPrintable((char *)
										 &(chunk->data[tag_offset+11]), taglen - 11);
                }

				// Non-fatal fail since beacon info might not have that
				// 11 byte leader on it, I don't know
            }

			// WPA frame matching if we have the privacy bit set
			if ((packinfo->cryptset & crypt_wep)) {
				// Liberally borrowed from Ethereal
				if ((tcitr = tag_cache_map.find(221)) != tag_cache_map.end()) {
					for (unsigned int tagct = 0; tagct < tcitr->second.size(); 
						 tagct++) {
						tag_offset = tcitr->second[tagct];
						unsigned int tag_orig = tag_offset + 1;
						unsigned int taglen = (chunk->data[tag_offset] & 0xFF);
						unsigned int offt = 0;

						if (tag_orig + taglen > chunk->length) {
							packinfo->corrupt = 1;
							in_pack->insert(_PCM(PACK_COMP_80211), packinfo);
							return 0;
						}

						// Match 221 tag header for WPA
						if (taglen < 6 || memcmp(&(chunk->data[tag_orig + offt]), 
												 WPA_OUI, sizeof(WPA_OUI)))
							continue;

						offt += 6;

						// Match WPA multicast suite
						if (offt + 4 > taglen || 
							memcmp(&(chunk->data[tag_orig + offt]), WPA_OUI,
								   sizeof(WPA_OUI)))
							continue;

						packinfo->cryptset |= 
							WPACipherConv(chunk->data[tag_orig + offt + 3]);

						// We don't care about parsing the number of ciphers,
						// we'll just iterate, so skip the cipher number
						offt += 6;

						// Match WPA unicast components
						while (offt + 4 <= taglen) {
							if (memcmp(&(chunk->data[tag_orig + offt]), 
									  WPA_OUI, sizeof(WPA_OUI)) == 0) {
								packinfo->cryptset |= 
									WPACipherConv(chunk->data[tag_orig + offt + 3]);
								offt += 4;
							} else {
								break;
							}
						}

						// Match auth key components
						offt += 2;
						while (offt + 4 <= taglen) {
							if (memcmp(&(chunk->data[tag_orig + offt]), 
									  WPA_OUI, sizeof(WPA_OUI)) == 0) {
								packinfo->cryptset |= 
									WPACipherConv(chunk->data[tag_orig + offt + 3]);
								offt += 4;
							} else {
								break;
							}
						}
					}
				} /* 221 */

				// Match tag 48 RSN WPA2
				if ((tcitr = tag_cache_map.find(48)) != tag_cache_map.end()) {
					for (unsigned int tagct = 0; tagct < tcitr->second.size(); 
						 tagct++) {
						tag_offset = tcitr->second[tagct];
						unsigned int tag_orig = tag_offset + 1;
						unsigned int taglen = (chunk->data[tag_offset] & 0xFF);
						unsigned int offt = 0;

						if (tag_orig + taglen > chunk->length || taglen < 6) {
							packinfo->corrupt = 1;
							in_pack->insert(_PCM(PACK_COMP_80211), packinfo);
							return 0;
						}

						// Skip version
						offt += 2;

						// Match multicast
						if (offt + 3 > taglen ||
							memcmp(&(chunk->data[tag_orig + offt]), RSN_OUI,
								   sizeof(RSN_OUI))) {
							packinfo->corrupt = 1;
							in_pack->insert(_PCM(PACK_COMP_80211), packinfo);
							return 0;
						}
						packinfo->cryptset |= 
							WPACipherConv(chunk->data[tag_orig + offt + 3]);
						offt += 4;

						// We don't care about unicast number
						offt += 2;

						while (offt + 4 <= taglen) {
							if (memcmp(&(chunk->data[tag_orig + offt]), 
									  RSN_OUI, sizeof(RSN_OUI)) == 0) {
								packinfo->cryptset |= 
									WPACipherConv(chunk->data[tag_orig + offt + 3]);
								offt += 4;
							} else {
								break;
							}
						}

						// We don't care about authkey number
						offt += 2;

						while (offt + 4 <= taglen) {
							if (memcmp(&(chunk->data[tag_orig + offt]), 
									  RSN_OUI, sizeof(RSN_OUI)) == 0) {
								packinfo->cryptset |= 
									WPAKeyMgtConv(chunk->data[tag_orig + offt + 3]);
								offt += 4;
							} else {
								break;
							}
						}
					}
				} /* 48 */
			}

            packinfo->dest_mac = addr0;
            packinfo->source_mac = addr1;
            packinfo->bssid_mac = addr2;

            // If beacons aren't do a broadcast destination, consider them corrupt.
            if (packinfo->dest_mac != broadcast_mac) 
                packinfo->corrupt = 1;
            
            // If beacons don't have a SSID and a basicrate then we consider them
            // corrupt
            if (found_ssid_tag == 0 || found_rate_tag == 0)
                packinfo->corrupt = 1;

            /*
            if (ret_packinfo->ess == 0) {
                // Weird adhoc beacon where the BSSID isn't 'right' so we use the 
				// source instead.
                ret_packinfo->bssid_mac = ret_packinfo->source_mac;
                ret_packinfo->distrib = adhoc_distribution;
                }
                */
        } else if (fc->subtype == 9) {
            // I'm not positive this is the right handling of atim packets.  
			// Do something smarter in the future
            packinfo->subtype = packet_sub_atim;

            packinfo->dest_mac = addr0;
            packinfo->source_mac = addr1;
            packinfo->bssid_mac = addr2;

            packinfo->distrib = distrib_unknown;

        } else if (fc->subtype == 10) {
            packinfo->subtype = packet_sub_disassociation;

            packinfo->dest_mac = addr0;
            packinfo->source_mac = addr1;
            packinfo->bssid_mac = addr2;

            uint16_t rcode;
            memcpy(&rcode, (const char *) &(chunk->data[24]), 2);

            packinfo->mgt_reason_code = rcode;

        } else if (fc->subtype == 11) {
            packinfo->subtype = packet_sub_authentication;

            packinfo->dest_mac = addr0;
            packinfo->source_mac = addr1;
            packinfo->bssid_mac = addr2;

            uint16_t rcode;
            memcpy(&rcode, (const char *) &(chunk->data[24]), 2);

            packinfo->mgt_reason_code = rcode;

        } else if (fc->subtype == 12) {
            packinfo->subtype = packet_sub_deauthentication;

            packinfo->dest_mac = addr0;
            packinfo->source_mac = addr1;
            packinfo->bssid_mac = addr2;

            uint16_t rcode;
            memcpy(&rcode, (const char *) &(chunk->data[24]), 2);

            packinfo->mgt_reason_code = rcode;
        } else {
            packinfo->subtype = packet_sub_unknown;
        }
    } else if (fc->type == 1) {
        packinfo->type = packet_phy;

        // Throw away large phy packets just like we throw away large management.
        // Phy stuff is all really small, so we set the limit smaller.
        if (chunk->length > 128) {
            packinfo->corrupt = 1;
			in_pack->insert(_PCM(PACK_COMP_80211), packinfo);
            return 0;
        }

        packinfo->distrib = distrib_unknown;

        if (fc->subtype == 11) {
            packinfo->subtype = packet_sub_rts;

        } else if (fc->subtype == 12) {
            packinfo->subtype = packet_sub_cts;

        } else if (fc->subtype == 13) {
            packinfo->subtype = packet_sub_ack;

            packinfo->dest_mac = addr0;

        } else if (fc->subtype == 14) {
            packinfo->subtype = packet_sub_cf_end;

        } else if (fc->subtype == 15) {
            packinfo->subtype = packet_sub_cf_end_ack;

        } else {
            packinfo->subtype = packet_sub_unknown;
        }

    } else if (fc->type == 2) {
        packinfo->type = packet_data;

        // Collect the subtypes - we probably want to do something better with thse
        // in the future
        if (fc->subtype == 0) {
            packinfo->subtype = packet_sub_data;

        } else if (fc->subtype == 1) {
            packinfo->subtype = packet_sub_data_cf_ack;

        } else if (fc->subtype == 2) {
            packinfo->subtype = packet_sub_data_cf_poll;

        } else if (fc->subtype == 3) {
            packinfo->subtype = packet_sub_data_cf_ack_poll;

        } else if (fc->subtype == 4) {
            packinfo->subtype = packet_sub_data_null;

        } else if (fc->subtype == 5) {
            packinfo->subtype = packet_sub_cf_ack;

        } else if (fc->subtype == 6) {
            packinfo->subtype = packet_sub_cf_ack_poll;
        } else {
            packinfo->corrupt = 1;
            packinfo->subtype = packet_sub_unknown;
			in_pack->insert(_PCM(PACK_COMP_80211), packinfo);
            return 0;
        }

        int datasize = chunk->length - packinfo->header_offset;
        if (datasize > 0)
            packinfo->datasize = datasize;

        // Extract ID's
        switch (packinfo->distrib) {
        case distrib_adhoc:
            packinfo->dest_mac = addr0;
            packinfo->source_mac = addr1;
            packinfo->bssid_mac = addr2;

            if (packinfo->bssid_mac.longmac == 0)
                packinfo->bssid_mac = packinfo->source_mac;

            packinfo->header_offset = 24;
            break;
        case distrib_from:
            packinfo->dest_mac = addr0;
            packinfo->bssid_mac = addr1;
            packinfo->source_mac = addr2;
            packinfo->header_offset = 24;
            break;
        case distrib_to:
            packinfo->bssid_mac = addr0;
            packinfo->source_mac = addr1;
            packinfo->dest_mac = addr2;
            packinfo->header_offset = 24;
            break;
        case distrib_unknown:
            // If we aren't long enough to hold a intra-ds packet, bail
            if (chunk->length < 30) {
                packinfo->corrupt = 1;
				in_pack->insert(_PCM(PACK_COMP_80211), packinfo);
                return 0;
            }

            packinfo->bssid_mac = addr1;
            packinfo->source_mac = addr3;
            packinfo->dest_mac = addr0;

            packinfo->distrib = distrib_inter;

            // First byte of offsets
            packinfo->header_offset = 30;
            break;
        default:
            packinfo->corrupt = 1;
			in_pack->insert(_PCM(PACK_COMP_80211), packinfo);
            return 0;
            break;
        }
	}

    // Do a little sanity checking on the BSSID
    if (packinfo->bssid_mac.error == 1 ||
        packinfo->source_mac.error == 1 ||
        packinfo->dest_mac.error == 1) {
        packinfo->corrupt = 1;
    }

	in_pack->insert(_PCM(PACK_COMP_80211), packinfo);

    return 1;
}

int KisBuiltinDissector::basicdata_dissector(kis_packet *in_pack) {
	kis_data_packinfo *datainfo = NULL;

	if (in_pack->error)
		return 0;

	// Grab the 80211 info, compare, bail
    kis_ieee80211_packinfo *packinfo;
	if ((packinfo = 
		 (kis_ieee80211_packinfo *) in_pack->fetch(_PCM(PACK_COMP_80211))) == NULL)
		return 0;
	if (packinfo->corrupt)
		return 0;
	if (packinfo->type != packet_data || packinfo->subtype != packet_sub_data)
		return 0;
	
	// Grab the mangled frame if we have it, then try to grab up the list of
	// data types and die if we can't get anything
	kis_datachunk *chunk = 
		(kis_datachunk *) in_pack->fetch(_PCM(PACK_COMP_MANGLEFRAME));

	if (chunk == NULL) {
		if ((chunk = 
			 (kis_datachunk *) in_pack->fetch(_PCM(PACK_COMP_80211FRAME))) == NULL) {
			if ((chunk = (kis_datachunk *) 
				 in_pack->fetch(_PCM(PACK_COMP_LINKFRAME))) == NULL) {
				return 0;
			}
		}
	}

	// Blow up on no content
    if (packinfo->header_offset > chunk->length)
        return 0;

	unsigned int header_offset = packinfo->header_offset;
	
	datainfo = new kis_data_packinfo;

	if (chunk->length > header_offset + LLC_UI_OFFSET + 
		sizeof(PROBE_LLC_SIGNATURE) && 
		memcmp(&(chunk->data[header_offset]), LLC_UI_SIGNATURE,
			   sizeof(LLC_UI_SIGNATURE)) == 0) {
		// Handle the batch of frames that fall under the LLC UI 0x3 frame
		if (memcmp(&(chunk->data[header_offset + LLC_UI_OFFSET]),
				   PROBE_LLC_SIGNATURE, sizeof(PROBE_LLC_SIGNATURE)) == 0) {

			// Packets that look like netstumber probes...
			if (header_offset + NETSTUMBLER_OFFSET + 
				sizeof(NETSTUMBLER_322_SIGNATURE) < chunk->length && 
				memcmp(&(chunk->data[header_offset + NETSTUMBLER_OFFSET]),
					   NETSTUMBLER_322_SIGNATURE, 
					   sizeof(NETSTUMBLER_322_SIGNATURE)) == 0) {
				_ALERT(netstumbler_aref, in_pack, packinfo,
					   "Detected Netstumbler 3.22 probe");
				datainfo->proto = proto_netstumbler_probe;
				datainfo->field1 = 322;
				in_pack->insert(_PCM(PACK_COMP_BASICDATA), datainfo);
				return 1;
			}

			if (header_offset + NETSTUMBLER_OFFSET + 
				sizeof(NETSTUMBLER_323_SIGNATURE) < chunk->length && 
				memcmp(&(chunk->data[header_offset + NETSTUMBLER_OFFSET]),
					   NETSTUMBLER_323_SIGNATURE, 
					   sizeof(NETSTUMBLER_323_SIGNATURE)) == 0) {
				_ALERT(netstumbler_aref, in_pack, packinfo,
					   "Detected Netstumbler 3.23 probe");
				datainfo->proto = proto_netstumbler_probe;
				datainfo->field1 = 323;
				in_pack->insert(_PCM(PACK_COMP_BASICDATA), datainfo);
				return 1;
			}

			if (header_offset + NETSTUMBLER_OFFSET + 
				sizeof(NETSTUMBLER_330_SIGNATURE) < chunk->length && 
				memcmp(&(chunk->data[header_offset + NETSTUMBLER_OFFSET]),
					   NETSTUMBLER_330_SIGNATURE, 
					   sizeof(NETSTUMBLER_330_SIGNATURE)) == 0) {
				_ALERT(netstumbler_aref, in_pack, packinfo,
					   "Detected Netstumbler 3.30 probe");
				datainfo->proto = proto_netstumbler_probe;
				datainfo->field1 = 330;
				in_pack->insert(_PCM(PACK_COMP_BASICDATA), datainfo);
				return 1;
			}

			if (header_offset + LUCENT_OFFSET + 
				sizeof(LUCENT_TEST_SIGNATURE) < chunk->length && 
				memcmp(&(chunk->data[header_offset + LUCENT_OFFSET]),
					   LUCENT_TEST_SIGNATURE, 
					   sizeof(LUCENT_TEST_SIGNATURE)) == 0) {
				_ALERT(lucenttest_aref, in_pack, packinfo,
					   "Detected Lucent probe/link test");
				datainfo->proto = proto_lucent_probe;
				in_pack->insert(_PCM(PACK_COMP_BASICDATA), datainfo);
				return 1;
			}

			_ALERT(netstumbler_aref, in_pack, packinfo,
				   "Detected what looks like a Netstumber probe but didn't "
				   "match known version fingerprint");
			datainfo->proto = proto_netstumbler_probe;
			datainfo->field1 = -1;

		} // LLC_SIGNATURE

		// We don't bail right here, if anything looks "more" like something 
		// else then we'll let it take over

	} // LLC_UI

	// CDP cisco discovery frames, good for finding unauthorized APs
	// +1 for the version frame we compare first
	if ((header_offset + LLC_UI_OFFSET + 1 +
		 sizeof(CISCO_SIGNATURE)) < chunk->length &&
		memcmp(&(chunk->data[header_offset + LLC_UI_OFFSET]), CISCO_SIGNATURE,
			   sizeof(CISCO_SIGNATURE)) == 0) {
		unsigned int offset = 0;

		// Look for frames the old way, maybe v1 used it?  Compare the versions.
		// I don't remember why the code worked this way.
		if (chunk->data[header_offset + LLC_UI_OFFSET + 
			sizeof(CISCO_SIGNATURE)] == 2)
			offset = header_offset + LLC_UI_OFFSET + sizeof(CISCO_SIGNATURE) + 4;
		else
			offset = header_offset + LLC_UI_OFFSET + 12;

		// Did we get useful info?
		int gotinfo = 0;

		while (offset + CDP_ELEMENT_LEN < chunk->length) {
		// uint16_t dot1x_length = kis_extract16(&(chunk->data[offset + 2]));
			uint16_t elemtype = kis_ntoh16(kis_extract16(&(chunk->data[offset + 0])));
			uint16_t elemlen = kis_ntoh16(kis_extract16(&(chunk->data[offset + 2])));

			if (elemlen == 0)
				break;

			if (offset + elemlen >= chunk->length)
				break;

			if (elemtype == 0x01) {
				// Device id, we care about this
				if (elemlen < 4) {
					_MSG("Corrupt CDP frame (possibly an exploit attempt), discarded",
						 MSGFLAG_ERROR);
					packinfo->corrupt = 1;
					delete(datainfo);
					return 0;
				}

				datainfo->cdp_dev_id = 
					MungeToPrintable((char *) &(chunk->data[offset + 4]), 
									 elemlen - 4);
				gotinfo = 1;
			} else if (elemtype == 0x03) {
				if (elemlen < 4) {
					_MSG("Corrupt CDP frame (possibly an exploit attempt), discarded",
						 MSGFLAG_ERROR);
					packinfo->corrupt = 1;
					delete(datainfo);
					return 0;
				}

				datainfo->cdp_port_id = 
					MungeToPrintable((char *) &(chunk->data[offset + 4]), 
									 elemlen - 4);
				gotinfo = 1;
			}

			offset += elemlen;
		}

		if (gotinfo) {
			datainfo->proto = proto_cdp;
			in_pack->insert(_PCM(PACK_COMP_BASICDATA), datainfo);
			return 1;
		}

	}

	// Dot1x frames
	// +1 for the version byte at header_offset + hot1x off
	// +3 for the offset past LLC_UI
	if ((header_offset + LLC_UI_OFFSET + 4 + 
		 sizeof(DOT1X_PROTO)) < chunk->length && 
		memcmp(&(chunk->data[header_offset + LLC_UI_OFFSET + 3]),
			   DOT1X_PROTO, sizeof(DOT1X_PROTO)) == 0) {
		// It's dot1x, is it LEAP?
		//
		// Make sure its an EAP socket
		unsigned int offset = header_offset + DOT1X_OFFSET;

		// Dot1x bits
		uint8_t dot1x_version = chunk->data[offset];
		uint8_t dot1x_type = chunk->data[offset + 1];
		// uint16_t dot1x_length = kis_extract16(&(chunk->data[offset + 2]));

		offset += EAP_OFFSET;

		if (dot1x_version != 1 || dot1x_type != 0 || 
			offset + EAP_PACKET_SIZE >= chunk->length) {
			delete datainfo;
			return 0;
		}

		// Eap bits
		uint8_t eap_code = chunk->data[offset];
		// uint8_t eap_id = chunk->data[offset + 1];
		// uint16_t eap_length = kis_extract16(&(chunk->data[offset + 2]));
		uint8_t eap_type = chunk->data[offset + 4];

		switch (eap_type) {
			case EAP_TYPE_LEAP:
				datainfo->proto = proto_leap;
				datainfo->field1 = eap_code;
				packinfo->cryptset |= crypt_leap;
				break;
			case EAP_TYPE_TLS:
				datainfo->proto = proto_tls;
				datainfo->field1 = eap_code;
				packinfo->cryptset |= crypt_tls;
				break;
			case EAP_TYPE_TTLS:
				datainfo->proto = proto_ttls;
				datainfo->field1 = eap_code;
				packinfo->cryptset |= crypt_ttls;
				break;
			case EAP_TYPE_PEAP:
				datainfo->proto = proto_peap;
				datainfo->field1 = eap_code;
				packinfo->cryptset |= crypt_peap;
				break;
			default:
				datainfo->proto = proto_eap_unknown;
				break;
		}

		in_pack->insert(_PCM(PACK_COMP_BASICDATA), datainfo);
		return 1;
	}

	if (header_offset + kismax(20, ARP_OFFSET + ARP_PACKET_SIZE) < chunk->length && 
		header_offset + ARP_OFFSET + sizeof(ARP_SIGNATURE) < chunk->length &&
		memcmp(&(chunk->data[header_offset + ARP_OFFSET]),
			   ARP_SIGNATURE, sizeof(ARP_SIGNATURE)) == 0) {
		// If we look like a ARP frame and we're big enough to be an arp 
		// frame...
		
		datainfo->proto = proto_arp;
		memcpy(&(datainfo->ip_source_addr.s_addr),
			   &(chunk->data[header_offset + 16]), 4);
		in_pack->insert(_PCM(PACK_COMP_BASICDATA), datainfo);
		return 1;
	}

	if (header_offset + kismax(UDP_OFFSET + 4, 
							   IP_OFFSET + 11) < chunk->length && 
		header_offset + IP_OFFSET + sizeof(TCP_SIGNATURE) < chunk->length &&
		memcmp(&(chunk->data[header_offset + IP_OFFSET]),
			   UDP_SIGNATURE, sizeof(UDP_SIGNATURE)) == 0) {

		// UDP frame...
		datainfo->ip_source_port = 
			kis_ntoh16(kis_extract16(&(chunk->data[header_offset + 
									   UDP_OFFSET])));
		datainfo->ip_dest_port = 
			kis_ntoh16(kis_extract16(&(chunk->data[header_offset + 
									   UDP_OFFSET + 2])));

		memcpy(&(datainfo->ip_source_addr.s_addr),
			   &(chunk->data[header_offset + IP_OFFSET + 3]), 4);
		memcpy(&(datainfo->ip_dest_addr.s_addr),
			   &(chunk->data[header_offset + IP_OFFSET + 7]), 4);

		if (datainfo->ip_source_port == IAPP_PORT &&
			datainfo->ip_dest_port == IAPP_PORT &&
			(header_offset + IAPP_OFFSET + 
			 IAPP_HEADER_SIZE) < chunk->length) {

			uint8_t iapp_version = 
				chunk->data[header_offset + IAPP_OFFSET];
			uint8_t iapp_type =
				chunk->data[header_offset + IAPP_OFFSET + 1];

			// If we can't understand the iapp version, bail and return the
			// UDP frame we DID decode
			if (iapp_version != 1) {
				in_pack->insert(_PCM(PACK_COMP_BASICDATA), datainfo);
				return 1;
			}

			// Same again -- bail on UDP if we can't make sense of this
			switch (iapp_type) {
				case iapp_announce_request:
				case iapp_announce_response:
				case iapp_handover_request:
				case iapp_handover_response:
					break;
				default:
					in_pack->insert(_PCM(PACK_COMP_BASICDATA), datainfo);
					return 1;
					break;
			}

			unsigned int pdu_offset = header_offset + IAPP_OFFSET +
				IAPP_HEADER_SIZE;

			while (pdu_offset + IAPP_PDUHEADER_SIZE < chunk->length) {
				uint8_t *pdu = &(chunk->data[pdu_offset]);
				uint8_t pdu_type = pdu[0];
				uint8_t pdu_len = pdu[1];

				// If we have a short/malformed PDU frame, bail
				if ((pdu_offset + 3 + pdu_len) >= chunk->length) {
					delete datainfo;
					return 0;
				}

				switch (pdu_type) {
					case iapp_pdu_ssid:
						if (pdu_len > SSID_SIZE)
							break;

						packinfo->ssid = 
							MungeToPrintable((char *) &(pdu[3]), pdu_len);
						break;
					case iapp_pdu_bssid:
						if (pdu_len != MAC_LEN)
							break;

						packinfo->bssid_mac = mac_addr(&(pdu[3]));
						break;
					case iapp_pdu_capability:
						if (pdu_len != 1)
							break;
						if ((pdu[3] & iapp_cap_wep))
							packinfo->cryptset |= crypt_wep;
						break;
					case iapp_pdu_channel:
						if (pdu_len != 1)
							break;
						packinfo->channel = (int) pdu[3];
						break;
					case iapp_pdu_beaconint:
						if (pdu_len != 2)
							break;
						packinfo->beacon_interval = (int) ((pdu[3] << 8) | pdu[4]);
						break;
					case iapp_pdu_oldbssid:
					case iapp_pdu_msaddr:
					case iapp_pdu_announceint:
					case iapp_pdu_hotimeout:
					case iapp_pdu_messageid:
					case iapp_pdu_phytype:
					case iapp_pdu_regdomain:
					case iapp_pdu_ouiident:
					case iapp_pdu_authinfo:
					default:
						break;
				}
				pdu_offset += pdu_len + 3;
			}

			datainfo->proto = proto_iapp;
			in_pack->insert(_PCM(PACK_COMP_BASICDATA), datainfo);
			return 1;
		} // IAPP port

		if ((datainfo->ip_source_port == ISAKMP_PORT ||
			 datainfo->ip_dest_port == ISAKMP_PORT) &&
			(header_offset + ISAKMP_OFFSET + 
			 ISAKMP_PACKET_SIZE) < chunk->length) {
			
			datainfo->proto = proto_isakmp;
			datainfo->field1 = 
				chunk->data[header_offset + ISAKMP_OFFSET + 4];

			packinfo->cryptset |= crypt_isakmp;
			
			in_pack->insert(_PCM(PACK_COMP_BASICDATA), datainfo);
			return 1;

		}

		if ((header_offset + DHCPD_OFFSET +
			 sizeof(DHCPD_SIGNATURE)) < chunk->length &&
			memcmp(&(chunk->data[header_offset + DHCPD_OFFSET]),
				   DHCPD_SIGNATURE, sizeof(DHCPD_SIGNATURE)) == 0) {
			datainfo->proto = proto_dhcp_offer;

			// Now we go through the dhcp options until we find 1, 3, and 53
			unsigned int offset = header_offset + DHCPD_OFFSET + 252;

			while ((offset + 1) < chunk->length) {
				if (chunk->data[offset] == 0x01) {
					// netmask
					// bail if we're a boring DHCP ack w/ no real content
					if (chunk->data[offset + 2] == 0x00) {
						datainfo->proto = proto_udp;
						in_pack->insert(_PCM(PACK_COMP_BASICDATA), datainfo);
						return 1;
					}

					// Bail if we're misformed
					if (offset + 6 >= chunk->length) {
						delete datainfo;
						return 0;
					}

					memcpy(&(datainfo->ip_netmask_addr.s_addr), 
						   &(chunk->data[offset + 2]), 4);
				} else if (chunk->data[offset] == 0x03) {
					// Gateway
					if (chunk->data[offset + 2] == 0x00) {
						datainfo->proto = proto_udp;
						in_pack->insert(_PCM(PACK_COMP_BASICDATA), datainfo);
						return 1;
					}

					// Bail if we're misformed
					if (offset + 6 >= chunk->length) {
						delete datainfo;
						return 0;
					}

					memcpy(&(datainfo->ip_gateway_addr.s_addr), 
						   &(chunk->data[offset + 2]), 4);
				} else if (chunk->data[offset] == 0x35) {
					// Offered addr
					
					// Boring
					if (chunk->data[offset + 2] == 0x00) {
						datainfo->proto = proto_udp;
						in_pack->insert(_PCM(PACK_COMP_BASICDATA), datainfo);
						return 1;
					}

					// Have to dig all the way into the bootp segment
					
					// Bail if we're misformed
					if (offset + 32 >= chunk->length) {
						delete datainfo;
						return 0;
					}

					memcpy(&(datainfo->ip_dest_addr.s_addr), 
						   &(chunk->data[offset + 28]), 4);
				}

				offset += chunk->data[offset + 1] + 2;
			}

			in_pack->insert(_PCM(PACK_COMP_BASICDATA), datainfo);
			return 1;
		} // DHCP

		in_pack->insert(_PCM(PACK_COMP_BASICDATA), datainfo);
		return 1;

	} // UDP frame

	if (header_offset + kismax(TCP_OFFSET + 4, 
							   IP_OFFSET + TCP_HEADER_SIZE) < chunk->length && 
		header_offset + IP_OFFSET + sizeof(TCP_SIGNATURE) < chunk->length &&
		memcmp(&(chunk->data[header_offset + IP_OFFSET]),
			   TCP_SIGNATURE, sizeof(TCP_SIGNATURE)) == 0) {

		// TCP frame...
		datainfo->ip_source_port = 
			kis_ntoh16(kis_extract16(&(chunk->data[header_offset + 
									   TCP_OFFSET])));
		datainfo->ip_dest_port = 
			kis_ntoh16(kis_extract16(&(chunk->data[header_offset + 
									   TCP_OFFSET + 2])));

		memcpy(&(datainfo->ip_source_addr.s_addr),
			   &(chunk->data[header_offset + IP_OFFSET + 3]), 4);
		memcpy(&(datainfo->ip_dest_addr.s_addr),
			   &(chunk->data[header_offset + IP_OFFSET + 7]), 4);

		datainfo->proto = proto_tcp;

		if (datainfo->ip_source_port == PPTP_PORT || 
			datainfo->ip_dest_port == PPTP_PORT) {
			datainfo->proto = proto_pptp;
			packinfo->cryptset |= crypt_pptp;
		}

		in_pack->insert(_PCM(PACK_COMP_BASICDATA), datainfo);
		return 1;
	} // TCP frame

	// Trash the data if we didn't fill it in
	delete(datainfo);

	return 1;
}

int KisBuiltinDissector::wep_data_decryptor(kis_packet *in_pack) {
	kis_datachunk *manglechunk = NULL;

	if (in_pack->error)
		return 0;

	// Grab the 80211 info, compare, bail
    kis_ieee80211_packinfo *packinfo;
	if ((packinfo = 
		 (kis_ieee80211_packinfo *) in_pack->fetch(_PCM(PACK_COMP_80211))) == NULL)
		return 0;
	if (packinfo->corrupt)
		return 0;
	if (packinfo->type != packet_data || packinfo->subtype != packet_sub_data)
		return 0;

	// No need to look at data thats already been decoded
	if (packinfo->cryptset == 0 || packinfo->decrypted == 1)
		return 0;

	// Grab the 80211 frame, if that doesn't exist, grab the link frame
	kis_datachunk *chunk = 
		(kis_datachunk *) in_pack->fetch(_PCM(PACK_COMP_80211FRAME));

	if (chunk == NULL) {
		if ((chunk = 
			 (kis_datachunk *) in_pack->fetch(_PCM(PACK_COMP_LINKFRAME))) == NULL) {
			return 0;
		}
	}

	// Bail on size check
	if (chunk->length < packinfo->header_offset ||
		chunk->length - packinfo->header_offset <= 8)
		return 0;

	// Bail if we can't find a key match
	macmap<wep_key_info *>::iterator bwmitr = wepkeys.find(packinfo->bssid_mac);
	if (bwmitr == wepkeys.end())
		return 0;

	// Password field
	char pwd[WEPKEY_MAX + 3];
	memset(pwd, 0, WEPKEY_MAX + 3);

	// Extract the IV and add it to the key
	pwd[0] = chunk->data[packinfo->header_offset + 0] & 0xFF;
	pwd[1] = chunk->data[packinfo->header_offset + 1] & 0xFF;
	pwd[2] = chunk->data[packinfo->header_offset + 2] & 0xFF;

	// Add the supplied password to the key
	memcpy(pwd + 3, (*bwmitr->second)->key, WEPKEY_MAX);
	int pwdlen = 3 + (*bwmitr->second)->len;

	// Prepare the keyblock for the rc4 cipher
	unsigned char keyblock[256];
	memcpy(keyblock, wep_identity, 256);
	int kba = 0, kbb = 0;
	for (kba = 0; kba < 256; kba++) {
		kbb = (kbb + keyblock[kba] + pwd[kba % pwdlen]) & 0xFF;
		unsigned char oldkey = keyblock[kba];
		keyblock[kba] = keyblock[kbb];
		keyblock[kbb] = oldkey;
	}

	// Allocate the mangled chunk -- 4 byte IV/Key# gone, 4 byte ICV gone
	manglechunk = new kis_datachunk;
	manglechunk->length = chunk->length - 8;
	manglechunk->data = new uint8_t[manglechunk->length];

	// Copy the packet headers to the new chunk
	memcpy(manglechunk->data, chunk->data, packinfo->header_offset);

	// Decrypt the data payload and check the CRC
	kba = kbb = 0;
	uint32_t crc = ~0;
	uint8_t c_crc[4];
	uint8_t icv[4];

	// Copy the ICV into the CRC buffer for checking
	memcpy(icv, &(chunk->data[chunk->length - 4]), 4);

	for (unsigned int dpos = packinfo->header_offset + 4; 
		 dpos < chunk->length - 4; dpos++) {
		kba = (kba + 1) & 0xFF;
		kbb = (kbb + keyblock[kba]) & 0xFF;

		unsigned char oldkey = keyblock[kba];
		keyblock[kba] = keyblock[kbb];
		keyblock[kbb] = oldkey;

		// Decode the byte into the pos - 4 (no wepkey header)
		manglechunk->data[dpos - 4] = 
			chunk->data[dpos] ^ keyblock[(keyblock[kba] + keyblock[kbb]) & 0xFF];

		crc = wep_crc32_table[(crc ^ manglechunk->data[dpos]) & 0xFF] ^ (crc >> 8);
	}

	// Check the CRC
	crc = ~crc;
	c_crc[0] = crc;
	c_crc[1] = crc >> 8;
	c_crc[2] = crc >> 16;
	c_crc[3] = crc >> 24;

	int crcfailure = 0;
	for (unsigned int crcpos = 0; crcpos < 4; crcpos++) {
		kba = (kba + 1) & 0xFF;
		kbb = (kbb + keyblock[kba]) & 0xFF;

		unsigned char oldkey = keyblock[kba];
		keyblock[kba] = keyblock[kbb];
		keyblock[kbb] = oldkey;

		if ((c_crc[crcpos] ^ keyblock[(keyblock[kba] + keyblock[kbb]) & 0xFF]) !=
			icv[crcpos]) {
			crcfailure = 1;
			break;
		}
	}

	// If the CRC check failed, delete the moddata
	if (crcfailure) {
		(*bwmitr->second)->failed++;
		delete manglechunk;
		return 0;
	}

	(*bwmitr->second)->decrypted++;
	packinfo->decrypted = 1;
	in_pack->insert(_PCM(PACK_COMP_MANGLEFRAME), manglechunk);
	return 1;
}

int KisBuiltinDissector::cmd_listwepkeys(CLIENT_PARMS) {
    if (client_wepkey_allowed == 0) {
        snprintf(errstr, 1024, "Server does not allow clients to fetch keys");
        return -1;
    }

    if (wepkeys.size() == 0) {
        snprintf(errstr, 1024, "Server has no WEP keys");
        return -1;
    }

    if (_NPM(PROTO_REF_WEPKEY) < 0) {
        snprintf(errstr, 1024, "Unable to find WEPKEY protocol");
        return -1;
    }
    
    for (macmap<wep_key_info *>::iterator wkitr = wepkeys.begin(); 
		 wkitr != wepkeys.end(); wkitr++) {
        globalreg->kisnetserver->SendToClient(in_clid, _NPM(PROTO_REF_WEPKEY), 
											  (void *) wkitr->second, NULL);
    }

    return 1;
}

int KisBuiltinDissector::cmd_addwepkey(CLIENT_PARMS) {
    if (parsedcmdline->size() != 1) {
        snprintf(errstr, 1024, "Illegal addwepkey request");
        return -1;
    }

    vector<string> keyvec = StrTokenize((*parsedcmdline)[1].word, ",");
    if (keyvec.size() != 2) {
        snprintf(errstr, 1024, "Illegal addwepkey request");
        return -1;
    }

    wep_key_info *winfo = new wep_key_info;
    winfo->fragile = 1;
    winfo->bssid = keyvec[0].c_str();

    if (winfo->bssid.error) {
        snprintf(errstr, 1024, "Illegal addwepkey bssid");
        return -1;
    }

    unsigned char key[WEPKEY_MAX];
    int len = Hex2UChar((unsigned char *) keyvec[1].c_str(), key);

    winfo->len = len;
    memcpy(winfo->key, key, sizeof(unsigned char) * WEPKEY_MAX);

    // Replace exiting ones
	if (wepkeys.find(winfo->bssid) != wepkeys.end())
		delete wepkeys[winfo->bssid];

	wepkeys.insert(winfo->bssid, winfo);

    snprintf(errstr, 1024, "Added key %s length %d for BSSID %s",
             (*parsedcmdline)[0].word.c_str(), len, 
			 winfo->bssid.Mac2String().c_str());

    _MSG(errstr, MSGFLAG_INFO);

    return 1;
}

int KisBuiltinDissector::cmd_delwepkey(CLIENT_PARMS) {
    if (client_wepkey_allowed == 0) {
        snprintf(errstr, 1024, "Server does not allow clients to modify keys");
        return -1;
    }

    if (parsedcmdline->size() != 1) {
        snprintf(errstr, 1024, "Illegal delwepkey command");
        return -1;
    }

    mac_addr bssid_mac = (*parsedcmdline)[0].word.c_str();

    if (bssid_mac.error) {
        snprintf(errstr, 1024, "Illegal delwepkey bssid");
        return -1;
    }

    if (wepkeys.find(bssid_mac) == wepkeys.end()) {
        snprintf(errstr, 1024, "Unknown delwepkey bssid");
        return -1;
    }

    delete wepkeys[bssid_mac];
    wepkeys.erase(bssid_mac);

    snprintf(errstr, 1024, "Deleted key for BSSID %s", 
             bssid_mac.Mac2String().c_str());
    _MSG(errstr, MSGFLAG_INFO);

    return 1;
}

