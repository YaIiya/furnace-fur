/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2022 tildearrow and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "es5506.h"
#include "../engine.h"
#include "../../ta-log.h"
#include <math.h>
#include <map>

#define CHIP_FREQBASE (16*2048*(chanMax+1))
#define NOTE_ES5506(c,note) (chan[c].pcm.freqOffs*NOTE_FREQUENCY(note))

#define rWrite(a,...) {if(!skipRegisterWrites) {hostIntf32.emplace(4,(a),__VA_ARGS__); }}
#define rRead(a,...) {hostIntf32.emplace(4,(a),__VA_ARGS__);}
#define immWrite(a,...) {hostIntf32.emplace(4,(a),__VA_ARGS__);}
#define pageWrite(p,a,...) \
  if (!skipRegisterWrites) { \
    if (curPage!=(p)) { \
      curPage=(p); \
      rWrite(0xf,curPage); \
    } \
    rWrite((a),__VA_ARGS__); \
  }

#define pageWriteMask(p,pm,a,...) \
  if (!skipRegisterWrites) { \
    if ((curPage&(pm))!=((p)&(pm))) { \
      curPage=(curPage&~(pm))|((p)&(pm)); \
      rWrite(0xf,curPage,(pm)); \
    } \
    rWrite((a),__VA_ARGS__); \
  }


const char* regCheatSheetES5506[]={
	"CR", "00|00",
	"FC", "00|01",
	"LVOL", "00|02",
	"LVRAMP", "00|03",
	"RVOL", "00|04",
	"RVRAMP", "00|05",
	"ECOUNT", "00|06",
	"K2", "00|07",
	"K2RAMP", "00|08",
	"K1", "00|09",
	"K1RAMP", "00|0A",
	"ACTV", "00|0B",
	"MODE", "00|0C",
	"POT", "00|0D",
	"IRQV", "00|0E",
	"PAGE", "00|0F",
	"CR", "20|00",
	"START", "20|01",
	"END", "20|02",
	"ACCUM", "20|03",
	"O4(n-1)", "20|04",
	"O3(n-2)", "20|05",
	"O3(n-1)", "20|06",
	"O2(n-2)", "20|07",
	"O2(n-1)", "20|08",
	"O1(n-1)", "20|09",
	"W_ST", "20|0A",
	"W_END", "20|0B",
	"LR_END", "20|0C",
	"POT", "20|0D",
	"IRQV", "20|0E",
	"PAGE", "20|0F",
	"CH0L", "40|00",
	"CH0R", "40|01",
	"CH1L", "40|02",
	"CH1R", "40|03",
	"CH2L", "40|04",
	"CH2R", "40|05",
	"CH3L", "40|06",
	"CH3R", "40|07",
	"CH4L", "40|08",
	"CH4R", "40|09",
	"CH5L", "40|0A",
	"CH5R", "40|0B",
	"POT", "40|0D",
	"IRQV", "40|0E",
	"PAGE", "40|0F",
  NULL
};

const char** DivPlatformES5506::getRegisterSheet() {
  return regCheatSheetES5506;
}

const char* DivPlatformES5506::getEffectName(unsigned char effect) {
  switch (effect) {
    case 0x10:
      return "10xx: Change waveform";
      break;
    case 0x11:
      return "11xx: Set filter mode (00 to 03)";
      break;
    case 0x20:
      return "20xx: Set envelope count (000 to 0FF)";
      break;
    case 0x21:
      return "21xx: Set envelope count (100 to 1FF)";
      break;
    case 0x22:
      return "22xx: Set envelope left volume ramp (signed)";
      break;
    case 0x23:
      return "23xx: Set envelope right volume ramp (signed)";
      break;
    case 0x24:
      return "24xx: Set envelope k1 ramp (signed)";
      break;
    case 0x25:
      return "25xx: Set envelope k1 ramp (signed, slower)";
      break;
    case 0x26:
      return "26xx: Set envelope k2 ramp (signed)";
      break;
    case 0x27:
      return "27xx: Set envelope k2 ramp (signed, slower)";
      break;
    default:
      if ((effect&0xf0)==0x30) {
        return "3xxx: Set filter K1";
      } else if ((effect&0xf0)==0x40) {
        return "4xxx: Set filter K2";
      }
  }
  return NULL;
}
void DivPlatformES5506::acquire(short* bufL, short* bufR, size_t start, size_t len) {
  for (size_t h=start; h<start+len; h++) {
    // convert 32 bit access to 8 bit host interface
    while (!hostIntf32.empty()) {
      QueuedHostIntf w=hostIntf32.front();
      if (w.isRead && (w.read!=NULL)) {
        hostIntf8.emplace(0,w.addr,w.read,w.mask);
        hostIntf8.emplace(1,w.addr,w.read,w.mask);
        hostIntf8.emplace(2,w.addr,w.read,w.mask);
        hostIntf8.emplace(3,w.addr,w.read,w.mask,w.delay);
      } else {
        hostIntf8.emplace(0,w.addr,w.val,w.mask);
        hostIntf8.emplace(1,w.addr,w.val,w.mask);
        hostIntf8.emplace(2,w.addr,w.val,w.mask);
        hostIntf8.emplace(3,w.addr,w.val,w.mask,w.delay);
      }
      hostIntf32.pop();
    }
    es5506.tick_perf();
    bufL[h]=es5506.lout(0);
    bufR[h]=es5506.rout(0);
  }
}

void DivPlatformES5506::e(bool state)
{
  if (es5506.e_rising_edge()) {
    if (cycle) { // wait until delay
      cycle--;
    } else if (!hostIntf8.empty()) {
      QueuedHostIntf w=hostIntf8.front();
      unsigned char shift=24-(w.step<<3);
      if (w.isRead) {
        *w.read=((*w.read)&(~((0xff<<shift)&w.mask)))|((es5506.host_r((w.addr<<2)+w.step)<<shift)&w.mask);
        if (w.step==3) {
          if (w.delay>0) {
            cycle+=w.delay;
          }
          isReaded=true;
        } else {
          isReaded=false;
        }
        hostIntf8.pop();
      } else {
        isReaded=false;
        unsigned int mask=(w.mask>>shift)&0xff;
        if ((mask==0xff) || isMasked) {
          if (mask==0xff) {
            maskedVal=(w.val>>shift)&0xff;
          }
          es5506.host_w((w.addr<<2)+w.step,maskedVal);
          if(dumpWrites) {
            addWrite((w.addr<<2)+w.step,maskedVal);
          }
          isMasked=false;
          if ((w.step==3) && (w.delay>0)) {
            cycle+=w.delay;
          }
          hostIntf8.pop();
        } else if (!isMasked) {
          maskedVal=((w.val>>shift)&mask)|(es5506.host_r((w.addr<<2)+w.step)&~mask);
          isMasked=true;
        }
      }
    }
  }
  if (isReaded) {
    isReaded=false;
    if (irqTrigger) {
      irqTrigger=false;
      if ((irqv&0x80)==0) {
        unsigned char ch=irqv&0x1f;
        if (chan[ch].isReverseLoop) { // Reversed loop
          pageWriteMask(0x00|ch,0x5f,0x00,(chan[ch].pcm.reversed?0x0000:0x0040)|0x08,0x78);
          chan[ch].isReverseLoop=false;
        }
      }
    }
  }
}

void DivPlatformES5506::irqb(bool state) {
  rRead(0x0e,&irqv,0x9f);
  irqTrigger=true;
}

void DivPlatformES5506::tick(bool sysTick) {
  for (int i=0; i<=chanMax; i++) {
    chan[i].std.next();
    DivInstrument* ins=parent->getIns(chan[i].ins);
    // volume/panning macros
    if (chan[i].std.vol.had) {
      chan[i].outVol=((chan[i].vol&0xff)*MIN(0xffff,chan[i].std.vol.val))/0xff;
      if (!isMuted[i]) {
        chan[i].volChanged=true;
      }
    }
    if (chan[i].std.panL.had) {
      chan[i].outLVol=(((ins->es5506.lVol*(chan[i].lVol&0xf))/0xf)*MIN(0xffff,chan[i].std.panL.val))/0xffff;
      if (!isMuted[i]) {
        chan[i].volChanged=true;
      }
    }
    if (chan[i].std.panR.had) {
      chan[i].outRVol=(((ins->es5506.rVol*(chan[i].rVol&0xf))/0xf)*MIN(0xffff,chan[i].std.panR.val))/0xffff;
      if (!isMuted[i]) {
        chan[i].volChanged=true;
      }
    }
    // arpeggio/pitch macros, frequency related
    if (chan[i].std.arp.had) {
      if (!chan[i].inPorta) {
        if (chan[i].std.arp.mode) {
          chan[i].baseFreq=NOTE_ES5506(i,chan[i].std.arp.val);
        } else {
          chan[i].baseFreq=NOTE_ES5506(i,chan[i].note+chan[i].std.arp.val);
        }
      }
      chan[i].freqChanged=true;
    } else {
      if (chan[i].std.arp.mode && chan[i].std.arp.finished) {
        chan[i].baseFreq=NOTE_ES5506(i,chan[i].note);
        chan[i].freqChanged=true;
      }
    }
    if (chan[i].std.pitch.had) {
      chan[i].freqChanged=true;
    }
    // phase reset macro
    if (chan[i].std.phaseReset.had) {
      if (chan[i].std.phaseReset.val==1) {
        chan[i].keyOn=true;
      }
    }
    // filter macros
    if (chan[i].std.duty.had) {
      if (chan[i].filter.mode!=DivInstrumentES5506::Filter::FilterMode(chan[i].std.duty.val&3)) {
        chan[i].filter.mode=DivInstrumentES5506::Filter::FilterMode(chan[i].std.duty.val&3);
        chan[i].filterChanged.mode=1;
      }
    }
    if (chan[i].std.ex1.had) {
      switch (chan[i].std.ex1.mode) {
        case 0: // relative
          if (chan[i].k1Offs!=chan[i].std.ex1.val) {
            chan[i].k1Offs=chan[i].std.ex1.val;
            chan[i].filterChanged.k1=1;
          }
        case 1: // absolute
          if (chan[i].filter.k1!=(chan[i].std.ex1.val&0xffff)) {
            chan[i].filter.k1=chan[i].std.ex1.val&0xffff;
            chan[i].filterChanged.k1=1;
          }
          break;
        case 2: { // delta
          signed int next_k1=MAX(0,MIN(65535,chan[i].filter.k1+chan[i].std.ex1.val));
          if (chan[i].filter.k1!=next_k1) {
            chan[i].filter.k1=next_k1;
            chan[i].filterChanged.k1=1;
          }
          break;
        }
        default:
          break;
      }
    }
    if (chan[i].std.ex2.had) {
      switch (chan[i].std.ex2.mode) {
        case 0: // relative
          if (chan[i].k2Offs!=chan[i].std.ex1.val) {
            chan[i].k2Offs=chan[i].std.ex1.val;
            chan[i].filterChanged.k2=1;
          }
        case 1: // absolute
          if (chan[i].filter.k2!=(chan[i].std.ex2.val&0xffff)) {
            chan[i].filter.k2=chan[i].std.ex2.val&0xffff;
            chan[i].filterChanged.k2=1;
          }
          break;
        case 2: { // delta
          signed int next_k2=MAX(0,MIN(65535,chan[i].filter.k2+chan[i].std.ex2.val));
          if (chan[i].filter.k2!=next_k2) {
            chan[i].filter.k2=next_k2;
            chan[i].filterChanged.k2=1;
          }
          break;
        }
        default:
          break;
      }
    }
    // envelope macros
    if (chan[i].std.ex3.had) {
      if (chan[i].envelope.ecount!=(chan[i].std.ex3.val&0x1ff)) {
        chan[i].envelope.ecount=chan[i].std.ex3.val&0x1ff;
        chan[i].envChanged.ecount=1;
      }
    }
    if (chan[i].std.ex4.had) {
      if (chan[i].envelope.lVRamp!=chan[i].std.ex4.val) {
        chan[i].envelope.lVRamp=chan[i].std.ex4.val;
        chan[i].envChanged.lVRamp=1;
      }
    }
    if (chan[i].std.ex5.had) {
      if (chan[i].envelope.rVRamp!=chan[i].std.ex5.val) {
        chan[i].envelope.rVRamp=chan[i].std.ex5.val;
        chan[i].envChanged.rVRamp=1;
      }
    }
    if (chan[i].std.ex6.had) {
      if (chan[i].envelope.k1Ramp!=chan[i].std.ex6.val) {
        chan[i].envelope.k1Ramp=chan[i].std.ex6.val;
        chan[i].envChanged.k1Ramp=1;
      }
    }
    if (chan[i].std.ex7.had) {
      if (chan[i].envelope.k2Ramp!=chan[i].std.ex7.val) {
        chan[i].envelope.k2Ramp=chan[i].std.ex7.val;
        chan[i].envChanged.k2Ramp=1;
      }
    }
    if (chan[i].std.ex8.had) {
      if (chan[i].envelope.k1Slow!=(chan[i].std.ex8.val&1)) {
        chan[i].envelope.k1Slow=chan[i].std.ex8.val&1;
        chan[i].envChanged.k1Ramp=1;
      }
      if (chan[i].envelope.k2Slow!=(chan[i].std.ex8.val&2)) {
        chan[i].envelope.k2Slow=chan[i].std.ex8.val&2;
        chan[i].envChanged.k2Ramp=1;
      }
    }
    // update registers
    if (chan[i].volChanged) {
      if (!isMuted[i]) { // calculate volume (16 bit)
        chan[i].resLVol=(chan[i].outVol*chan[i].outLVol)/0xffff;
        chan[i].resRVol=(chan[i].outVol*chan[i].outRVol)/0xffff;
        if (!chan[i].keyOn) {
          pageWrite(0x00|i,0x02,chan[i].resLVol);
          pageWrite(0x00|i,0x04,chan[i].resRVol);
        }
      } else { // mute
        pageWrite(0x00|i,0x02,0);
        pageWrite(0x00|i,0x04,0);
      }
      chan[i].volChanged=false;
    }
    if (chan[i].filterChanged.changed) {
      if (!chan[i].keyOn) {
        if (chan[i].filterChanged.mode) {
          pageWriteMask(0x00|i,0x5f,0x00,(chan[i].filter.mode<<8),0x0300);
        }
        if (chan[i].filterChanged.k2) {
          if (chan[i].std.ex2.mode==0) { // Relative
            pageWrite(0x00|i,0x07,MAX(0,MIN(65535,chan[i].filter.k2+chan[i].k2Offs)));
          } else {
            pageWrite(0x00|i,0x07,chan[i].filter.k2);
          }
        }
        if (chan[i].filterChanged.k1) {
          if (chan[i].std.ex1.mode==0) { // Relative
            pageWrite(0x00|i,0x09,MAX(0,MIN(65535,chan[i].filter.k1+chan[i].k1Offs)));
          } else {
            pageWrite(0x00|i,0x09,chan[i].filter.k1);
          }
        }
      }
      chan[i].filterChanged.changed=0;
    }
    if (chan[i].envChanged.changed) {
      if (!chan[i].keyOn) {
          if (chan[i].envChanged.lVRamp) {
            pageWrite(0x00|i,0x03,((unsigned char)chan[i].envelope.lVRamp)<<8);
          }
          if (chan[i].envChanged.rVRamp) {
            pageWrite(0x00|i,0x05,((unsigned char)chan[i].envelope.rVRamp)<<8);
          }
          if (chan[i].envChanged.ecount) {
            pageWrite(0x00|i,0x06,chan[i].envelope.ecount);
          }
          if (chan[i].envChanged.k2Ramp) {
            pageWrite(0x00|i,0x08,(((unsigned char)chan[i].envelope.k2Ramp)<<8)|(chan[i].envelope.k2Slow?1:0));
          }
          if (chan[i].envChanged.k1Ramp) {
            pageWrite(0x00|i,0x0a,(((unsigned char)chan[i].envelope.k1Ramp)<<8)|(chan[i].envelope.k1Slow?1:0));
          }
      }
      chan[i].envChanged.changed=0;
    }
    if (chan[i].freqChanged || chan[i].keyOn || chan[i].keyOff) {
      chan[i].freq=parent->calcFreq(chan[i].baseFreq,chan[i].pitch,false)+chan[i].std.pitch.val;
      if (chan[i].freq<0) chan[i].freq=0;
      if (chan[i].freq>0x1ffff) chan[i].freq=0x1ffff;
      if (chan[i].keyOn) {
        if (chan[i].pcm.index>=0) {
          pageWriteMask(0x00|i,0x5f,0x00,0x0303); // Wipeout CR
          pageWrite(0x00|i,0x06,0); // Clear ECOUNT
          pageWrite(0x20|i,0x03,chan[i].pcm.reversed?chan[i].pcm.end:chan[i].pcm.start); // Set ACCUM to start address
          pageWrite(0x00|i,0x07,0xffff); // Set K1 and K2 to 0xffff
          pageWrite(0x00|i,0x09,0xffff,~0,(chanMax+1)*4*2); // needs to 4 sample period delay
          pageWrite(0x00|i,0x01,chan[i].freq);
          pageWrite(0x20|i,0x01,(chan[i].pcm.loopMode==DIV_SAMPLE_LOOPMODE_ONESHOT)?chan[i].pcm.start:chan[i].pcm.loopStart);
          pageWrite(0x20|i,0x02,(chan[i].pcm.loopMode==DIV_SAMPLE_LOOPMODE_ONESHOT)?chan[i].pcm.end:chan[i].pcm.loopEnd);
          // initialize envelope
          pageWrite(0x00|i,0x03,((unsigned char)chan[i].envelope.lVRamp)<<8);
          pageWrite(0x00|i,0x05,((unsigned char)chan[i].envelope.rVRamp)<<8);
          pageWrite(0x00|i,0x0a,(((unsigned char)chan[i].envelope.k1Ramp)<<8)|(chan[i].envelope.k1Slow?1:0));
          pageWrite(0x00|i,0x08,(((unsigned char)chan[i].envelope.k2Ramp)<<8)|(chan[i].envelope.k2Slow?1:0));
          // initialize filter
          pageWriteMask(0x00|i,0x5f,0x00,(chan[i].pcm.bank<<14)|(chan[i].filter.mode<<8),0xc300);
          if ((chan[i].std.ex2.mode==0) && (chan[i].std.ex2.had)) {
            pageWrite(0x00|i,0x07,MAX(0,MIN(65535,chan[i].filter.k2+chan[i].k2Offs)));
          } else {
            pageWrite(0x00|i,0x07,chan[i].filter.k2);
          }
          if ((chan[i].std.ex1.mode==0) && (chan[i].std.ex1.had)) {
            pageWrite(0x00|i,0x09,MAX(0,MIN(65535,chan[i].filter.k1+chan[i].k1Offs)));
          } else {
            pageWrite(0x00|i,0x09,chan[i].filter.k1);
          }
          pageWrite(0x00|i,0x02,chan[i].resLVol);
          pageWrite(0x00|i,0x04,chan[i].resRVol);
          unsigned int loopFlag=chan[i].pcm.reversed?0x0040:0x0000;
          chan[i].isReverseLoop=false;
          switch (chan[i].pcm.loopMode) {
            case DIV_SAMPLE_LOOPMODE_ONESHOT: // One shot (no loop)
            default:
              break;
            case DIV_SAMPLE_LOOPMODE_FOWARD: // Foward loop
              loopFlag|=0x0008;
              break;
            case DIV_SAMPLE_LOOPMODE_BACKWARD: // Backward loop: IRQ enable
              loopFlag|=0x0038;
              chan[i].isReverseLoop=true;
              break;
            case DIV_SAMPLE_LOOPMODE_PINGPONG: // Pingpong loop: Hardware support
              loopFlag|=0x0018;
              break;
          }
          // Run sample
          pageWrite(0x00|i,0x06,chan[i].envelope.ecount); // Clear ECOUNT
          pageWriteMask(0x00|i,0x5f,0x00,loopFlag,0x3cff);
        }
      }
      if (chan[i].keyOff) {
        pageWriteMask(0x00|i,0x5f,0x00,0x0303); // Wipeout CR
      } else if (chan[i].active) {
        pageWrite(0x00|i,0x01,chan[i].freq);
      }
      if (chan[i].keyOn) chan[i].keyOn=false;
      if (chan[i].keyOff) chan[i].keyOff=false;
      chan[i].freqChanged=false;
    }
  }
}

int DivPlatformES5506::dispatch(DivCommand c) {
  switch (c.cmd) {
    case DIV_CMD_NOTE_ON: {
      DivInstrument* ins=parent->getIns(chan[c.chan].ins);
      if (chan[c.chan].insChanged) {
        chan[c.chan].sample=ins->amiga.useNoteMap?ins->amiga.noteMap[c.value].ind:ins->amiga.initSample;
        double off=1.0;
        if (chan[c.chan].sample>=0 && chan[c.chan].sample<parent->song.sampleLen) {
          chan[c.chan].pcm.index=chan[c.chan].sample;
          DivSample* s=parent->getSample(chan[c.chan].sample);
          if (s->centerRate<1) {
            off=1.0;
          } else {
            off=ins->amiga.useNoteMap?((double)ins->amiga.noteMap[c.value].freq/((double)s->centerRate*pow(2.0,((double)c.value-48.0)/12.0))):((double)s->centerRate/8363.0);
          }
          const unsigned int start=s->offES5506<<10;
          const unsigned int length=s->samples-1;
          const unsigned int end=start+(length<<11);
          chan[c.chan].pcm.loopMode=s->isLoopable()?s->loopMode:DIV_SAMPLE_LOOPMODE_ONESHOT;
          chan[c.chan].pcm.freqOffs=off;
          chan[c.chan].pcm.reversed=ins->amiga.reversed;
          chan[c.chan].pcm.bank=(s->offES5506>>22)&3;
          chan[c.chan].pcm.start=start;
          chan[c.chan].pcm.end=end;
          chan[c.chan].pcm.length=length;
          chan[c.chan].pcm.loopStart=(start+(s->loopStart<<11))&0xfffff800;
          chan[c.chan].pcm.loopEnd=(start+((s->loopEnd-1)<<11))&0xffffff80;
          chan[c.chan].filter=ins->es5506.filter;
          chan[c.chan].envelope=ins->es5506.envelope;
        } else {
          chan[c.chan].sample=-1;
          chan[c.chan].pcm.index=-1;
          chan[c.chan].filter=DivInstrumentES5506::Filter();
          chan[c.chan].envelope=DivInstrumentES5506::Envelope();
        }
        chan[c.chan].insChanged=false;
      }
      if (c.value!=DIV_NOTE_NULL) {
        chan[c.chan].baseFreq=NOTE_ES5506(c.chan,c.value);
        chan[c.chan].freqChanged=true;
        chan[c.chan].volChanged=true;
        chan[c.chan].note=c.value;
      }
      if (!chan[c.chan].std.vol.will) {
        chan[c.chan].outVol=(0xffff*chan[c.chan].vol)/0xff;
      }
      if (!chan[c.chan].std.panL.will) {
        chan[c.chan].outLVol=(ins->es5506.lVol*chan[c.chan].lVol)/0xf;
      }
      if (!chan[c.chan].std.panR.will) {
        chan[c.chan].outRVol=(ins->es5506.rVol*chan[c.chan].rVol)/0xf;
      }
      chan[c.chan].active=true;
      chan[c.chan].keyOn=true;
      chan[c.chan].std.init(ins);
      break;
    }
    case DIV_CMD_NOTE_OFF:
      chan[c.chan].filter=DivInstrumentES5506::Filter();
      chan[c.chan].envelope=DivInstrumentES5506::Envelope();
      chan[c.chan].sample=-1;
      chan[c.chan].active=false;
      chan[c.chan].keyOff=true;
      chan[c.chan].std.init(NULL);
      break;
    case DIV_CMD_NOTE_OFF_ENV:
    case DIV_CMD_ENV_RELEASE:
      chan[c.chan].std.release();
      break;
    case DIV_CMD_INSTRUMENT:
      if (chan[c.chan].ins!=c.value || c.value2==1) {
        chan[c.chan].ins=c.value;
      }
      break;
    case DIV_CMD_VOLUME:
      if (chan[c.chan].vol!=c.value) {
        chan[c.chan].vol=c.value;
        if (!chan[c.chan].std.vol.has) {
          chan[c.chan].outVol=(0xffff*c.value)/0xff;
          if (!isMuted[c.chan]) {
            chan[c.chan].volChanged=true;
          }
        }
      }
      break;
    case DIV_CMD_GET_VOLUME:
      if (chan[c.chan].std.vol.has) {
        return chan[c.chan].vol;
      }
      return chan[c.chan].outVol;
      break;
    case DIV_CMD_PANNING: {
      DivInstrument* ins=parent->getIns(chan[c.chan].ins);
      // 08LR, each nibble means volume multipler for each channels
      // Left volume
      unsigned char lVol=(c.value>>4)&0xf;
      if (chan[c.chan].lVol!=lVol) {
        chan[c.chan].lVol=lVol;
        if (!chan[c.chan].std.panL.has) {
          chan[c.chan].outLVol=(ins->es5506.lVol*lVol)/0xf;
          if (!isMuted[c.chan]) {
            chan[c.chan].volChanged=true;
          }
        }
      }
      // Right volume
      unsigned char rVol=(c.value>>0)&0xf;
      if (chan[c.chan].rVol!=rVol) {
        chan[c.chan].rVol=rVol;
        if (!chan[c.chan].std.panR.has) {
          chan[c.chan].outRVol=(ins->es5506.rVol*rVol)/0xf;
          if (!isMuted[c.chan]) {
            chan[c.chan].volChanged=true;
          }
        }
      }
      break;
    }
    case DIV_CMD_PITCH:
      chan[c.chan].pitch=c.value;
      chan[c.chan].freqChanged=true;
      break;
    case DIV_CMD_WAVE:
      // reserved for useWave
      break;
    // Filter commands
    case DIV_CMD_ES5506_FILTER_MODE:
      chan[c.chan].filter.mode=DivInstrumentES5506::Filter::FilterMode(c.value&3);
      chan[c.chan].filterChanged.mode=1;
      break;
    case DIV_CMD_ES5506_FILTER_K1:
      chan[c.chan].filter.k1=(chan[c.chan].filter.k1&0xf)|((c.value&0xfff)<<4);
      chan[c.chan].filterChanged.k1=1;
      break;
    case DIV_CMD_ES5506_FILTER_K2:
      chan[c.chan].filter.k2=(chan[c.chan].filter.k2&0xf)|((c.value&0xfff)<<4);
      chan[c.chan].filterChanged.k2=1;
      break;
    // Envelope commands
    case DIV_CMD_ES5506_ENVELOPE_COUNT:
      chan[c.chan].envelope.ecount=c.value&0x1ff;
      chan[c.chan].envChanged.ecount=1;
      break;
    case DIV_CMD_ES5506_ENVELOPE_LVRAMP:
      chan[c.chan].envelope.lVRamp=(signed char)(c.value&0xff);
      chan[c.chan].envChanged.lVRamp=1;
      break;
    case DIV_CMD_ES5506_ENVELOPE_RVRAMP:
      chan[c.chan].envelope.rVRamp=(signed char)(c.value&0xff);
      chan[c.chan].envChanged.rVRamp=1;
      break;
    case DIV_CMD_ES5506_ENVELOPE_K1RAMP:
      chan[c.chan].envelope.k1Ramp=(signed char)(c.value&0xff);
      chan[c.chan].envelope.k1Slow=c.value2&1;
      chan[c.chan].envChanged.k1Ramp=1;
      break;
    case DIV_CMD_ES5506_ENVELOPE_K2RAMP:
      chan[c.chan].envelope.k2Ramp=(signed char)(c.value&0xff);
      chan[c.chan].envelope.k2Slow=c.value2&1;
      chan[c.chan].envChanged.k2Ramp=1;
      break;
    case DIV_CMD_NOTE_PORTA: {
      int destFreq=NOTE_ES5506(c.chan,c.value2);
      bool return2=false;
      if (destFreq>chan[c.chan].baseFreq) {
        chan[c.chan].baseFreq+=c.value;
        if (chan[c.chan].baseFreq>=destFreq) {
          chan[c.chan].baseFreq=destFreq;
          return2=true;
        }
      } else {
        chan[c.chan].baseFreq-=c.value;
        if (chan[c.chan].baseFreq<=destFreq) {
          chan[c.chan].baseFreq=destFreq;
          return2=true;
        }
      }
      chan[c.chan].freqChanged=true;
      if (return2) {
        chan[c.chan].inPorta=false;
        return 2;
      }
      break;
    }
    case DIV_CMD_LEGATO: {
      chan[c.chan].baseFreq=NOTE_ES5506(c.chan,c.value+((chan[c.chan].std.arp.will && !chan[c.chan].std.arp.mode)?(chan[c.chan].std.arp.val-12):(0)));
      chan[c.chan].freqChanged=true;
      chan[c.chan].note=c.value;
      break;
    }
    case DIV_CMD_PRE_PORTA:
      if (chan[c.chan].active && c.value2) {
        if (parent->song.resetMacroOnPorta) chan[c.chan].std.init(parent->getIns(chan[c.chan].ins));
      }
      chan[c.chan].inPorta=c.value;
      break;
    case DIV_CMD_SAMPLE_POS: {
      if (chan[c.chan].useWave) break;
      if (chan[c.chan].active) {
        unsigned int pos=chan[c.chan].pcm.reversed?(chan[c.chan].pcm.length-c.value):c.value;
        if ((chan[c.chan].pcm.reversed && pos>0) || ((!chan[c.chan].pcm.reversed) && pos<chan[c.chan].pcm.length)) {
          pageWrite(0x20|c.chan,0x03,chan[c.chan].pcm.start+(pos<<11));
        }
      }
      break;
    }
    case DIV_CMD_GET_VOLMAX:
      return 255;
      break;
    case DIV_ALWAYS_SET_VOLUME:
      return 1;
      break;
    default:
      break;
  }
  return 1;
}

void DivPlatformES5506::muteChannel(int ch, bool mute) {
  isMuted[ch]=mute;
  es5506.set_mute(ch,mute);
}

void DivPlatformES5506::forceIns() {
  for (int i=0; i<=chanMax; i++) {
    chan[i].insChanged=true;
    chan[i].freqChanged=true;
    chan[i].volChanged=true;
    chan[i].filterChanged.changed=(unsigned char)(~0);
    chan[i].envChanged.changed=(unsigned char)(~0);
    chan[i].sample=-1;
  }
}

void* DivPlatformES5506::getChanState(int ch) {
  return &chan[ch];
}

void DivPlatformES5506::reset() {
  while (!hostIntf32.empty()) hostIntf32.pop();
  while (!hostIntf8.empty()) hostIntf8.pop();
  for (int i=0; i<32; i++) {
    chan[i]=DivPlatformES5506::Channel();
    chan[i].std.setEngine(parent);
  }
  es5506.reset();
  for (int i=0; i<32; i++) {
    es5506.set_mute(i,isMuted[i]);
  }

  cycle=0;
  curPage=0;
  maskedVal=0;
  irqv=0x80;
  isMasked=false;
  isReaded=false;
  irqTrigger=false;
  chanMax=initChanMax;

  pageWriteMask(0x00,0x60,0x0b,chanMax);
  pageWriteMask(0x00,0x60,0x0b,0x1f);
  pageWriteMask(0x20,0x60,0x0a,0x01);
  pageWriteMask(0x20,0x60,0x0b,0x11);
  pageWriteMask(0x20,0x60,0x0c,0x20);
  pageWriteMask(0x00,0x60,0x0c,0x08); // Reset serial output
}

bool DivPlatformES5506::isStereo() {
  return true;
}

bool DivPlatformES5506::keyOffAffectsArp(int ch) {
  return true;
}

void DivPlatformES5506::notifyInsChange(int ins) {
  for (int i=0; i<32; i++) {
    if (chan[i].ins==ins) {
      chan[i].insChanged=true;
    }
  }
}

void DivPlatformES5506::notifyWaveChange(int wave) {
  // TODO when wavetables are added
  // TODO they probably won't be added unless the samples reside in RAM
}

void DivPlatformES5506::notifyInsDeletion(void* ins) {
  for (int i=0; i<32; i++) {
    chan[i].std.notifyInsDeletion((DivInstrument*)ins);
  }
}

void DivPlatformES5506::setFlags(unsigned int flags) {
  initChanMax=MAX(4,flags&0x1f);
  chanMax=initChanMax;
  pageWriteMask(0x00,0x60,0x0b,chanMax);
}

void DivPlatformES5506::poke(unsigned int addr, unsigned short val) {
  immWrite(addr, val);
}

void DivPlatformES5506::poke(std::vector<DivRegWrite>& wlist) {
  for (DivRegWrite& i: wlist) immWrite(i.addr,i.val);
}

unsigned char* DivPlatformES5506::getRegisterPool() {
  unsigned char* regPoolPtr = regPool;
  for (unsigned char p=0; p<128; p++) {
    for (unsigned char r=0; r<16; r++) {
      unsigned int reg=es5506.regs_r(p,r,false);
      for (int b=0; b<4; b++) {
        *regPoolPtr++ = reg>>(24-(b<<3));
      }
    }
  }
  return regPool;
}

int DivPlatformES5506::getRegisterPoolSize() {
  return 4*16*128; // 7 bit page x 16 registers per page x 32 bit per registers
}

int DivPlatformES5506::init(DivEngine* p, int channels, int sugRate, unsigned int flags) {
  parent=p;
  dumpWrites=false;
  skipRegisterWrites=false;

  for (int i=0; i<32; i++) {
    isMuted[i]=false;
  }
  setFlags(flags);

  chipClock=16000000;
  rate=chipClock/16; // 2 E clock tick (16 CLKIN tick) per voice
  reset();
  return 32;
}

void DivPlatformES5506::quit() {
}
