/*  
 *  readit.cpp
 *
 *  Copyright (c) 2010 Motiejus Jakštys
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.

 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <math.h>
#include <algorithm>
#include <stdlib.h>
#include <cstdio>
#include <map>
#include <set>
#include <list>
#include <vector>
#include <string>
#include <string.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <jack/jack.h>

using namespace std;

#define SRC_WAV 0
#define SRC_JACK_ONE 1
#define SRC_JACK_AUTO 2
#define ACTION_DUMP 0
#define ACTION_CATCH 1

#define BUFFERSIZE 1

#define ACTION_FN_ARGS int w, double place, double len, unsigned long int b
#define ACTION_FN(func) void (*func)(ACTION_FN_ARGS)

void fatal(void * r) {
    char * msg = (char*) r;
    printf (msg);
    exit (1);
};
char errmsg[200];   // Temporary message

struct sVolumes {
    unsigned long head, tail;
	jack_default_audio_sample_t min, max;
    bool proc;
};

typedef struct {
    jack_default_audio_sample_t * buf;
    jack_nframes_t nframes;
} buffer;

class SoundPatty;
class Input {
    public:
        int SAMPLE_RATE, DATA_SIZE;
        virtual buffer * giveInput(buffer *) {
            fatal((void*)"This should never be called!!!");
        };
        virtual void test() {
            printf("Called Input\n");
        }
    protected:
        SoundPatty * _sp_inst;
};

class SoundPatty {
    public:
        SoundPatty(const char * fn) { 
            gSCounter = gMCounter = 0;
            read_cfg(fn);
        };
        map<string, double> cfg;
        virtual void Error(void*);
        int setInput(int, void*);
        int go(int, ACTION_FN(callback));
        int WAVE, CHUNKSIZE;
        unsigned long gMCounter, // How many matches we found
                      gSCounter; // How many samples we skipped
        void search_patterns (jack_default_audio_sample_t * buf, jack_nframes_t nframes, ACTION_FN(callback));
        vector<sVolumes> volume;
    private:
        Input * _input;
        int read_cfg(const char*);
        int source_app;
        char *input_params;
};

class WavInput : public Input {
    public:
        WavInput(SoundPatty * inst, void * args) {
            _sp_inst = inst;
            char * filename = (char*) args;
            process_headers(filename);
            // ------------------------------------------------------------
            // Adjust _sp_ volume 
            // Jack has float (-1..1) while M$ WAV has (-2^15..2^15)
            //
            for (vector<sVolumes>::iterator vol = _sp_inst->volume.begin(); vol != _sp_inst->volume.end(); vol++) {
                vol->min *= (1<<15);
                vol->max *= (1<<15);
            }
        }

        buffer * giveInput(buffer *buf_prop) {
            int16_t buf [SAMPLE_RATE * BUFFERSIZE]; // Process buffer every BUFFERSIZE secs
            if (feof(_fh)) {
                return NULL;
            }
            size_t read_size = fread(buf, 2, SAMPLE_RATE * BUFFERSIZE, _fh);

            // :HACK: fix this, do not depend on jack types where you don't need!
            jack_default_audio_sample_t buf2 [SAMPLE_RATE * BUFFERSIZE];
            for(int i = 0; i < read_size; i++) {
                buf2[i] = (jack_default_audio_sample_t)buf[i];
            }
            buf_prop->buf = buf2;
            buf_prop->nframes = read_size;
            return buf_prop;
        }
    protected:
        int process_headers(const char * infile) {/*{{{*/

            _fh = fopen(infile, "rb");
            if (_fh == NULL) {
                sprintf(errmsg, "Failed to open file %s, exiting\n", infile);
                fatal((void*)errmsg);
            }

            // Read bytes [0-3] and [8-11], they should be "RIFF" and "WAVE" in ASCII
            char header[5];
            fread(header, 1, 4, _fh);

            char sample[] = "RIFF";
            if (! check_sample(sample, header) ) {
                sprintf(errmsg, "RIFF header not found in %s, exiting\n", infile);
                fatal((void*)errmsg);
            }
            // Checking for compression code (21'st byte is 01, 22'nd - 00, little-endian notation
            uint16_t tmp[2]; // two-byte integer
            fseek(_fh, 0x14, 0); // offset = 20 bytes
            fread(&tmp, 2, 2, _fh); // Reading two two-byte samples (comp. code and no. of channels)
            if ( tmp[0] != 1 ) {
                sprintf(errmsg, "Only PCM(1) supported, compression code given: %d\n", tmp[0]);
                fatal ((void*)errmsg);
            }
            // Number of channels must be "MONO"
            if ( tmp[1] != 1 ) {
                sprintf(errmsg, "Only MONO supported, given number of channels: %d\n", tmp[1]);
                fatal ((void*)errmsg);
            }
            fread(&SAMPLE_RATE, 2, 1, _fh); // Single two-byte sample
            _sp_inst->WAVE = (int)SAMPLE_RATE * _sp_inst->cfg["minwavelen"];
            _sp_inst->CHUNKSIZE = _sp_inst->cfg["chunklen"] * (int)SAMPLE_RATE;
            fseek(_fh, 0x22, 0);
            uint16_t BitsPerSample;
            fread(&BitsPerSample, 1, 2, _fh);
            if (BitsPerSample != 16) {
                sprintf(errmsg, "Only 16-bit WAVs supported, given: %d\n", BitsPerSample);
                fatal((void*)errmsg);
            }
            // Get data chunk size here
            fread(header, 1, 4, _fh);
            strcpy(sample, "data");

            if (! check_sample(sample, header)) {
                fatal ((void*)"data chunk not found in byte offset=36, file corrupted.");
            }
            int DATA_SIZE;
            fread(&DATA_SIZE, 4, 1, _fh); // Single two-byte sample
            return 0;
        }

        bool check_sample (const char * sample, const char * b) { // My STRCPY.
            for(int i = 0; sample[i] != '\0'; i++) {
                if (sample[i] != b[i]) {
                    return false;
                }
            }
            return true;
        }/*}}}*/
    private:
        FILE *_fh;

};
void SoundPatty::Error(void * msg) {/*{{{*/
    char * mesg = (char*) msg;
    printf(mesg);
    exit(0);
}/*}}}*/
int SoundPatty::read_cfg (const char * filename) {/*{{{*/
    ifstream file;
    file.open(filename);
    string line;
    int x;
    while (! file.eof() ) {
        getline(file, line);
        x = line.find(":");
        if (x == -1) break; // Last line, exit
        istringstream i(line.substr(x+2));
        double tmp; i >> tmp;
        cfg[line.substr(0,x)] = tmp;
    }
    // Change cfg["treshold\d+_(min|max)"] to
    // something more compatible with sVolumes map
    sVolumes tmp;
    tmp.head = tmp.tail = tmp.max = tmp.min = tmp.proc = 0;
    volume.assign(cfg.size(), tmp); // Make a bit more then nescesarry
    int max_index = 0; // Number of different tresholds
    for(map<string, double>::iterator C = cfg.begin(); C != cfg.end(); C++) {
        // Failed to use boost::regex :(
        if (C->first.find("treshold") == 0) {
            istringstream tmp(C->first.substr(8));
            int i; tmp >> i;
            max_index = max(max_index, i);
            // C->second and volume[i].m{in,ax} are double
            if (C->first.find("_min") != -1) {
                volume[i].min = C->second;
            } else {
                volume[i].max = C->second;
            }
        }
    }
    volume.assign(volume.begin(), volume.begin()+max_index+1);
    return 0;
}/*}}}*/
int SoundPatty::setInput(const int source_app, void * input_params) {/*{{{*/
    if (0 <= source_app && source_app <= 2) {
        this->source_app = source_app;
    }
    switch(this->source_app) {
        case SRC_WAV:
            _input = new WavInput(this, input_params);
            break;
    }
    return 0;
}/*}}}*/

int SoundPatty::go(const int action, ACTION_FN(callback)) {
    string which_timeout (action == ACTION_DUMP?"sampletimeout" : "catchtimeout");
    buffer buf;

    buf.buf = NULL;
    buf.nframes = 0;
    while (_input->giveInput(&buf) != NULL) { // Have pointer to data

        search_patterns(buf.buf, buf.nframes, callback);
        if (gSCounter/_input->SAMPLE_RATE > cfg[which_timeout]) {
            return 0;
        }
    }
}

void SoundPatty::search_patterns (jack_default_audio_sample_t * buf, jack_nframes_t nframes, ACTION_FN(callback)) {
    for (int i = 0; i < nframes; gSCounter++, i++) {
        jack_default_audio_sample_t cur = buf[i]<0?-buf[i]:buf[i];

        int v = 0; // Counter for volume
        for (vector<sVolumes>::iterator V = volume.begin(); V != volume.end(); V++, v++) {
            if (V->min <= cur && cur <= V->max) {
                // ------------------------------------------------------------
                // If it's first item in this wave (proc = processing started)
                //
                if (!V->proc) {
                    V->tail = gSCounter;
                    V->proc = true;
                }
                // ------------------------------------------------------------
                // Here we are just normally in the wave.
                //
                V->head = gSCounter;
            } else { // We are not in the wave
                if (V->proc && (V->min < 0.001 || gSCounter - V->head > WAVE)) {

                    //------------------------------------------------------------
                    // This wave is over
                    //
                    if (gSCounter - V->tail >= CHUNKSIZE) {
                        // ------------------------------------------------------------
                        // The previous chunk is big enough to be noticed
                        //
                        callback(v, (double)V->tail/_input->SAMPLE_RATE, (double)(V->head - V->tail)/_input->SAMPLE_RATE, gMCounter++);
                    } 
                    // ------------------------------------------------------------
                    // Else it is too small, but we don't want to do anything in that case
                    // So therefore we just say that wave processing is over
                    //
                    V->proc = false;
                }
            }
        }
    }
}

void dump_out(ACTION_FN_ARGS) {
    printf ("%d;%.6f;%.6f\n", w, place, len);
}

int main (int argc, char *argv[]) {
    if (argc < 3) {
        fatal ((void*)"Usage: ./readit config.cfg sample.wav\nor\n"
                "./readit config.cfg samplefile.txt catchable.wav\n"
                "./readit config.cfg samplefile.txt jack jack\n");
    }
    if (argc == 3) {
        SoundPatty * pat = new SoundPatty(argv[1]); // usually config.cfg
        pat->setInput(SRC_WAV, argv[2]);
        switch (pat->go(ACTION_DUMP, dump_out)) {
            case 0: // It's just over. Either timeout or eof reached
                exit(0);
        }
    }
    exit(0);
}
