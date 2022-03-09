#include <iostream>
#include <fstream>
#include <typeinfo>
#include <cstring>
#include <cstdint>
#include <bitset>
#include <algorithm>
#include <vector>

#include <cstdlib>
#include <cinttypes>
#include <cstring>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <inttypes.h>
}



using namespace std;

/* MP4 output */
static AVFormatContext *fctx;
static AVStream *vStream;

static int working = 0;
static int max_width = 0, max_height = 0, fps = 0;


/* Helpers to decode Exp-Golomb */
static uint32_t janus_pp_h264_eg_getbit(uint8_t *base, uint32_t offset) {
	return ((*(base + (offset >> 0x3))) >> (0x7 - (offset & 0x7))) & 0x1;
}

static uint32_t janus_pp_h264_eg_decode(uint8_t *base, uint32_t *offset) {
	uint32_t zeros = 0;
	while(janus_pp_h264_eg_getbit(base, (*offset)++) == 0)
		zeros++;
	uint32_t res = 1 << zeros;
	int32_t i = 0;
	for(i=zeros-1; i>=0; i--) {
		res |= janus_pp_h264_eg_getbit(base, (*offset)++) << i;
	}
	return res-1;
}

/* Helper to parse a SPS (only to get the video resolution) */
static void janus_pp_h264_parse_sps(uint8_t *buffer, int *width, int *height) {
	/* Let's check if it's the right profile, first */
	int index = 1;
	int profile_idc = *(buffer+index);
	if(profile_idc != 66) {
		cout << "Profile is not baseline ("<< profile_idc<<"!= 66)\n"<<endl;
	}
	/* Then let's skip 2 bytes and evaluate/skip the rest */
	index += 3;
	uint32_t offset = 0;
	uint8_t *base = (uint8_t *)(buffer+index);
	/* Skip seq_parameter_set_id */
	janus_pp_h264_eg_decode(base, &offset);
	if(profile_idc >= 100) {
		/* Skip chroma_format_idc */
		janus_pp_h264_eg_decode(base, &offset);
		/* Skip bit_depth_luma_minus8 */
		janus_pp_h264_eg_decode(base, &offset);
		/* Skip bit_depth_chroma_minus8 */
		janus_pp_h264_eg_decode(base, &offset);
		/* Skip qpprime_y_zero_transform_bypass_flag */
		janus_pp_h264_eg_getbit(base, offset++);
		/* Skip seq_scaling_matrix_present_flag */
		janus_pp_h264_eg_getbit(base, offset++);
	}
	/* Skip log2_max_frame_num_minus4 */
	janus_pp_h264_eg_decode(base, &offset);
	/* Evaluate pic_order_cnt_type */
	int pic_order_cnt_type = janus_pp_h264_eg_decode(base, &offset);
	if(pic_order_cnt_type == 0) {
		/* Skip log2_max_pic_order_cnt_lsb_minus4 */
		janus_pp_h264_eg_decode(base, &offset);
	} else if(pic_order_cnt_type == 1) {
		/* Skip delta_pic_order_always_zero_flag, offset_for_non_ref_pic,
		 * offset_for_top_to_bottom_field and num_ref_frames_in_pic_order_cnt_cycle */
		janus_pp_h264_eg_getbit(base, offset++);
		janus_pp_h264_eg_decode(base, &offset);
		janus_pp_h264_eg_decode(base, &offset);
		int num_ref_frames_in_pic_order_cnt_cycle = janus_pp_h264_eg_decode(base, &offset);
		int i = 0;
		for(i=0; i<num_ref_frames_in_pic_order_cnt_cycle; i++) {
			janus_pp_h264_eg_decode(base, &offset);
		}
	}
	/* Skip max_num_ref_frames and gaps_in_frame_num_value_allowed_flag */
	janus_pp_h264_eg_decode(base, &offset);
	janus_pp_h264_eg_getbit(base, offset++);
	/* We need the following three values */
	int pic_width_in_mbs_minus1 = janus_pp_h264_eg_decode(base, &offset);
	int pic_height_in_map_units_minus1 = janus_pp_h264_eg_decode(base, &offset);
	int frame_mbs_only_flag = janus_pp_h264_eg_getbit(base, offset++);
	if(!frame_mbs_only_flag) {
		/* Skip mb_adaptive_frame_field_flag */
		janus_pp_h264_eg_getbit(base, offset++);
	}
	/* Skip direct_8x8_inference_flag */
	janus_pp_h264_eg_getbit(base, offset++);
	/* We need the following value to evaluate offsets, if any */
	int frame_cropping_flag = janus_pp_h264_eg_getbit(base, offset++);
	int frame_crop_left_offset = 0, frame_crop_right_offset = 0,
		frame_crop_top_offset = 0, frame_crop_bottom_offset = 0;
	if(frame_cropping_flag) {
		frame_crop_left_offset = janus_pp_h264_eg_decode(base, &offset);
		frame_crop_right_offset = janus_pp_h264_eg_decode(base, &offset);
		frame_crop_top_offset = janus_pp_h264_eg_decode(base, &offset);
		frame_crop_bottom_offset = janus_pp_h264_eg_decode(base, &offset);
	}
	/* Skip vui_parameters_present_flag */
	janus_pp_h264_eg_getbit(base, offset++);

	/* We skipped what we didn't care about and got what we wanted, compute width/height */
	if(width)
		*width = ((pic_width_in_mbs_minus1 +1)*16) - frame_crop_left_offset*2 - frame_crop_right_offset*2;
	if(height)
		*height = ((2 - frame_mbs_only_flag)* (pic_height_in_map_units_minus1 +1) * 16) - (frame_crop_top_offset * 2) - (frame_crop_bottom_offset * 2);
}

void reverseArray(uint8_t *array, int len) {
    int inc = len-1;
    cout << "reverseArray | len:" << len <<endl;
    for (int i=0; i<len; i++) {
        cout << i << char(array[i]) << endl;
    }
    cout << endl;
    for (int i=0; i<len/2; i++) {
        uint8_t temp;
        temp = array[i];
        array[i] = array[i+inc];
        array[i+inc] = temp;
        inc -=1;
    }
    cout << "after reversing " << endl;
    for (int i=0; i<len; i++) {
        cout << i << char(array[i]) << endl;
    }
    cout << endl;
}

int create_h264_context(void) {
    av_register_all();
    /* Video output */
	fctx = avformat_alloc_context();
    if(fctx == NULL) {
		cout << "Error allocating context\n";
		return -1;
	}
    vStream = avformat_new_stream(fctx, 0);
	if(vStream == NULL) {
		cout << "Error adding stream\n";
		return -1;
	}

    cout << "Creating h264 context succeeded!\n";
}

void decoding_video_frames() {

}

int main() {

    int res = create_h264_context();
    if (res < 0) {
        cout << "Failed to create h264 context!\n";
        return -1;
    }
    

    // int bytes = 0, skip = 0;
    long offset = 0;

    char *mjr_parser = new char[8];
    uint16_t mjr_header_len = 0;
    char *mjr_header;

    /*
    char prebuffer[1500];
    memset(prebuffer, 0, 1500);
    char prebuffer2[1500];
    memset(prebuffer2, 0, 1500); // for the padding data
    */

    ifstream fin("/Users/seunguklee/Dev/cpp/mjrs/us-oregon_dev_UhQUm5ArtG_aaODNx48hd_interview@camera_business_y4q7uAciLl_0_1633352852433-video.mjr", ios::binary | ios::in);
    
    working = 1;

    // 8바이트씩 mjr file read
    fin.read(mjr_parser, 8);

    cout << endl << "##########################################################" << endl;
    // Printing 'MJR0002' Flag
    for (int i=0; i<8; i++) {
        cout << *(mjr_parser+i);
    }

    // expection 'MJR00002'
    cout << "\nfpointer:" << (int)fin.tellg() << endl;
    // get mjr header length
    fin.read((char*)&mjr_header_len, sizeof(uint16_t));
    mjr_header_len = ntohs(mjr_header_len);// all integer value from bytes need to be converted to host sequence
    cout << mjr_header_len  << " | " << (int)fin.tellg() << endl;
    // get mjr header json
    mjr_header = new char[mjr_header_len];
    fin.read(mjr_header, mjr_header_len);
    cout << "MJR Header: " << mjr_header << endl;

    /* variables for saving data from loop */
    int len = 0, frameLen = 0;
    int bytes = 0, numBytes = 0;

    /* Start iterating mjr body to parse rtp packets */
    while(working && fin.peek() != EOF) {
        char *meet = new char[4];
        uint32_t rcvt = 0;
        uint16_t blen = 0;

        fin.read(meet, 4);
        cout << meet << endl;
        fin.read((char*)&rcvt, 4);
        cout << "rcvd time: " << rcvt << endl;
        fin.read((char*)&blen, 2);
        blen = ntohs(blen); // all integer value from bytes need to be converted to host sequence
        cout << "MJR Body Len: " << blen << endl;

        
        // check current pointer location
        int loc = fin.tellg();
        cout << "cur: " << loc << endl;
        fin.seekg(0, ios::end);
        int end = fin.tellg();
        cout << "end: " << end << endl;
        int body_len = end - loc;
        fin.seekg(loc);

        cout << "body_len: " << body_len << " | cur:"<< fin.tellg() << endl << endl;
        
        // create rtp buffer
        uint8_t *buffer = new uint8_t[blen];
        fin.read((char *)buffer, blen);


        /* ### Parsing rtp header ### */

        int version = *buffer & 0xc0;
        bool p = *buffer & 0x20;
        bool x = *buffer & 0x10;
        int cc = *buffer & 0x0F; 
        bool m = *(buffer+1) & 0x80;
        int pt = *(buffer+1) & 0x7F;

        // arranging bytes as host byte sequence from network byte sequence
        uint8_t *snr = new uint8_t[2];
        snr[0] = *(buffer+2) & 0xFF;
        snr[1] = *(buffer+3) & 0xFF;
        uint16_t sn;
        memcpy(&sn, snr, 2);
        sn = ntohs(sn);
        delete[] snr;
        
        uint8_t *tsr = new uint8_t[4];
        tsr[0] = *(buffer+4) & 0xFF;
        tsr[1] = *(buffer+5) & 0xFF;
        tsr[2] = *(buffer+6) & 0xFF;
        tsr[3] = *(buffer+7) & 0xFF;
        uint32_t ts;
        memcpy(&ts, tsr, 4);
        ts = ntohl(ts);
        delete[] tsr;

        uint8_t *ssrcr = new uint8_t[4];
        ssrcr[0] = *(buffer+8) & 0xFF;
        ssrcr[1] = *(buffer+9) & 0xFF;
        ssrcr[2] = *(buffer+10) & 0xFF;
        ssrcr[3] = *(buffer+11) & 0xFF;
        uint32_t ssrc;
        memcpy(&ssrc, ssrcr, 4);
        ssrc = ntohl(ssrc);
        delete[] ssrcr;

        cout << "  << RTP Header >>" << endl << "version: " << version << " | p: " << p << " | x: " << x
            << " | cc: " << cc << " | m: " << m << " | pt: " << pt << " | sn: " 
            << sn << " | ts: " << ts << " | ssrc: " << ssrc << endl << endl;

        buffer += 12;
        len += 12;

        vector<int> cids;
        for (int i=0; i<cc; i++) {
            uint32_t cid;
            uint8_t *cidr = new uint8_t[4];
            cidr[0] = *(buffer) & 0xFF;
            cidr[1] = *(buffer+1) & 0xFF;
            cidr[2] = *(buffer+2) & 0xFF;
            cidr[3] = *(buffer+3) & 0xFF;
            memcpy(&cid, cidr, 4);
            cid = ntohl(cid);
            cids.push_back(cid);
            buffer += 4;
            len += 3;
        }

        uint16_t hexid;
        uint16_t hexlen;
        uint16_t hst;
        if (x) {
            buffer += 8;
            len += 8;
        }

        len = blen - len;

        /* ### Parsing NAL Unit 

            The structure and semantics of the NAL unit header were introduced in
            Section 1.3.  For convenience, the format of the NAL unit header is
            reprinted below:

                +---------------+
                |0|1|2|3|4|5|6|7|
                +-+-+-+-+-+-+-+-+
                |F|NRI|  Type   |
                +---------------+

            This section specifies the semantics of F and NRI according to this
            specification.
        
            First byte:  [ 3 NAL UNIT BITS | 5 FRAGMENT TYPE BITS]     // NAL UNIT header
            Second byte: [ START BIT | RESERVED BIT | END BIT | 5 NAL UNIT BITS]
            Other bytes: [... VIDEO FRAGMENT DATA...]
        */

        int fb = (*buffer & 0x80) >> 7;
        int nri = (*buffer & 0x60) >> 5;
        int nlu0 = (*buffer & 0xE0) >> 5;

        /* H.264 depay */
        int jump = 0;
        int fragment = *buffer & 0x1F;
        int nal = *(buffer+1) & 0x1F;
        int start_bit = *(buffer+1) & 0x80;

        uint8_t * received_frame;

        buffer += 1;

        cout << "   << NAL Unit Data >> " << endl << "fb: " << fb << " | nri: " 
            << nri << endl;

        cout << "fragment: " << fragment << " | nal:" << nal << " | start_bit: " << start_bit << endl<<endl;

        if(fragment == 28 || fragment == 29)
            cout << "Fragment="<<fragment<<" NAL="<<nal<<" Start="<<start_bit<<" (len="<<len<<", frameLen="<<frameLen<<endl;
        else
            cout <<"Fragment="<<fragment<<" (len="<<len<<", frameLen="<<frameLen<<")"<<endl;
		
        if ((fragment > 0) && (fragment < 24)) {
            // meaning IDR. just put them in to the frame.
            uint8_t *temp = received_frame + frameLen;
            memset(temp, 0x00, 1);
            memset(temp + 1, 0x00, 1);
            memset(temp + 2, 0x01, 1);
            frameLen += 3;
        } else if (fragment == 7) {
            /* SPS, see if we can extract the width/height as well */
			cout << "Parsind width/height\n";
			int width = 0, height = 0;
			janus_pp_h264_parse_sps(buffer, &width, &height);
			if(width*height > max_width*max_height) {
				max_width = width;
				max_height = height;
			}
            numBytes = max_width * max_height * 3;
        } else if (fragment == 24) {
            // meaing STAP-A 
            cout << "Parsing STAP-A...\n";

            // buffer++;
            int tot = len - 1;
            uint16_t psize = 0;
            cout << "Start tot:" << tot << endl;
            while(tot > 0) {
                memcpy(&psize, buffer, 2);
                psize = ntohs(psize);
                cout << "psize: " << psize << endl;
                buffer += 2;
                tot -= 2;
                int nal = *buffer & 0x1F;
                
                if (numBytes && (frameLen + psize) >= numBytes) {
                    cout << "Invalid size " << frameLen << "+ " << psize << endl; 
                    continue;
                }         

                cout << "NAL_UNIT in STAP-A: " << nal << endl;
                
                if (nal == 7) {
                    /* SPS unit could be in STAP-A */
                    cout << "Parsind width/height\n";
                    int width = 0, height = 0;
                    janus_pp_h264_parse_sps(buffer, &width, &height);
                    if(width*height > max_width*max_height) {
						max_width = width;
						max_height = height;
					}
                    numBytes = max_width * max_height * 3;
                    received_frame = new uint8_t[numBytes]();
                    cout << "numBytes is set up from SPS: " << numBytes << endl; 
                } else if (nal == 8) {
                    cout << "Found PPS as well" << endl;
                }

                /* Now we have a single NAL */
                uint8_t *temp = received_frame + frameLen;
                memset(temp, 0x00, 1);
                memset(temp + 1, 0x00, 1);
                memset(temp + 2, 0x01, 1);
                frameLen += 3;
                memcpy(received_frame + frameLen, buffer, psize);
                frameLen += psize;
                /* Go on */
                buffer += psize;
                tot -= psize;
            }
        } else if ((fragment == 28) || (fragment == 29)) {
            uint8_t indicator = *buffer;
            uint8_t header = *(buffer+1);
            jump = 2;
            len -= 2;
            if(header & 0x80) { // start_bit is true
                /* First part of fragmented packet (S bit set) */
                uint8_t *temp = received_frame + frameLen;
                // set start bytes
                memset(temp, 0x00, 1);
                memset(temp + 1, 0x00, 1);
                memset(temp + 2, 0x01, 1);
                memset(temp + 3, (indicator & 0xE0) | (header & 0x1F), 1);
                frameLen += 4;
            } else if (header & 0x40) {
                /* Last part of fragmented packet (E bit set) */
            }
        }

        memcpy(received_frame + frameLen, buffer+jump, len);
        frameLen += len;

        if(len == 0) break;

        /* Handling current stacked frames */
        

        working = 0;
    }


    cout << endl;
    fin.close();
    return 0;
}
