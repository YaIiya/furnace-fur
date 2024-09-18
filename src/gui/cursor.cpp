/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2024 tildearrow and contributors
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

#include "gui.h"
#include "../ta-log.h"
#include "actionUtil.h"

void FurnaceGUI::startSelection(int xCoarse, int xFine, int y, bool fullRow) {
  DETERMINE_FIRST_LAST;

  if (xCoarse!=selStart.xCoarse || xFine!=selStart.xFine || y!=selStart.y) {
    curNibble=false;
  }

  if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !fullRow && settings.doubleClickColumn) {
    if (cursor.xCoarse==xCoarse && cursor.xFine==xFine && cursor.y==y) {
      // select entire channel
      selStart.xCoarse=xCoarse;
      selStart.xFine=0;
      selStart.y=0;
      selEnd.xCoarse=xCoarse;
      selEnd.xFine=2+e->curPat[selEnd.xCoarse].effectCols*2;
      selEnd.y=e->curSubSong->patLen-1;

      finishSelection();
      return;
    }
  }

  if ((settings.dragMovesSelection==1 || (settings.dragMovesSelection==2 && (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)))) && !fullRow) {
    if (xCoarse>=selStart.xCoarse && (xFine>=selStart.xFine || xCoarse>selStart.xCoarse) && y>=selStart.y &&
        xCoarse<=selEnd.xCoarse && (xFine<=selEnd.xFine || xCoarse<selEnd.xCoarse) && y<=selEnd.y) {
      dragging=true;
      selecting=true;
      selectingFull=false;
      dragSourceX=xCoarse;
      dragSourceXFine=xFine;
      dragSourceY=y;
      dragDestinationX=xCoarse;
      dragDestinationXFine=xFine;
      dragDestinationY=y;
      dragStart=selStart;
      dragEnd=selEnd;
      return;
    }
  }
  
  if (fullRow) {
    selStart.xCoarse=firstChannel;
    selStart.xFine=0;
    selEnd.xCoarse=lastChannel-1;
    selEnd.xFine=2+e->curPat[selEnd.xCoarse].effectCols*2;
    selStart.y=y;
    selEnd.y=y;
  } else {
    if (xCoarse!=cursor.xCoarse || y!=cursor.y) {
      makeCursorUndo();
    }
    cursor.xCoarse=xCoarse;
    cursor.xFine=xFine;
    cursor.y=y;
    selStart.xCoarse=xCoarse;
    selStart.xFine=xFine;
    selStart.y=y;
    selEnd.xCoarse=xCoarse;
    selEnd.xFine=xFine;
    selEnd.y=y;
  }
  selecting=true;
  selectingFull=fullRow;
  e->setMidiBaseChan(cursor.xCoarse);
}

void FurnaceGUI::updateSelection(int xCoarse, int xFine, int y, bool fullRow) {
  if (!selecting) return;
  if (dragging) {
    dragDestinationX=xCoarse;
    if (dragStart.xFine>=3 && dragStart.xCoarse==dragEnd.xCoarse) dragDestinationXFine=(dragSourceXFine&1)?((xFine-1)|1):((xFine+1)&(~1));
    dragDestinationY=y;
    cursorDrag.xCoarse=xCoarse;
    cursorDrag.xFine=xFine;
    cursorDrag.y=y;

    int len=dragEnd.xCoarse-dragStart.xCoarse+1;
    if (len<0) len=0;

    DETERMINE_FIRST_LAST;

    if (dragStart.xCoarse+(dragDestinationX-dragSourceX)<firstChannel) {
      dragDestinationX=dragSourceX-dragStart.xCoarse;
    }

    if (dragEnd.xCoarse+(dragDestinationX-dragSourceX)>=lastChannel) {
      dragDestinationX=lastChannel-(dragEnd.xCoarse-dragSourceX)-1;
    }

    if (dragStart.xFine>=3 && dragStart.xCoarse==dragEnd.xCoarse) {
      if (dragEnd.xFine+(dragDestinationXFine-dragSourceXFine)>(2+e->curPat[dragDestinationX].effectCols*2)) {
        dragDestinationXFine=(2+e->curPat[dragDestinationX].effectCols*2)-dragEnd.xFine+dragSourceXFine;
      }
      if (dragStart.xFine+(dragDestinationXFine-dragSourceXFine)<3) {
        dragDestinationXFine=3-dragStart.xFine+dragSourceXFine;
      } 
    }

    if (dragStart.y+(dragDestinationY-dragSourceY)<0) {
      dragDestinationY=dragSourceY-dragStart.y;
    }

    if (dragEnd.y+(dragDestinationY-dragSourceY)>=e->curSubSong->patLen) {
      dragDestinationY=e->curSubSong->patLen-(dragEnd.y-dragSourceY)-1;
    }

    selStart.xCoarse=dragStart.xCoarse+(dragDestinationX-dragSourceX);
    selStart.xFine=dragStart.xFine+(dragDestinationXFine-dragSourceXFine);
    selStart.y=dragStart.y+(dragDestinationY-dragSourceY);
    selEnd.xCoarse=dragEnd.xCoarse+(dragDestinationX-dragSourceX);
    selEnd.xFine=dragEnd.xFine+(dragDestinationXFine-dragSourceXFine);
    selEnd.y=dragEnd.y+(dragDestinationY-dragSourceY);
  } else {
    if (selectingFull) {
      DETERMINE_LAST;
      selEnd.xCoarse=lastChannel-1;
      selEnd.xFine=2+e->curPat[selEnd.xCoarse].effectCols*2;
      selEnd.y=y;
    } else {
      selEnd.xCoarse=xCoarse;
      selEnd.xFine=xFine;
      selEnd.y=y;
    }
  }
}

void FurnaceGUI::finishSelection() {
  // swap points if needed
  if (selEnd.y<selStart.y) {
    selEnd.y^=selStart.y;
    selStart.y^=selEnd.y;
    selEnd.y^=selStart.y;
  }
  if (selEnd.xCoarse<selStart.xCoarse) {
    selEnd.xCoarse^=selStart.xCoarse;
    selStart.xCoarse^=selEnd.xCoarse;
    selEnd.xCoarse^=selStart.xCoarse;

    selEnd.xFine^=selStart.xFine;
    selStart.xFine^=selEnd.xFine;
    selEnd.xFine^=selStart.xFine;
  } else if (selEnd.xCoarse==selStart.xCoarse && selEnd.xFine<selStart.xFine) {
    selEnd.xFine^=selStart.xFine;
    selStart.xFine^=selEnd.xFine;
    selEnd.xFine^=selStart.xFine;
  }
  selecting=false;
  selectingFull=false;

  if (dragging) {
    if (dragSourceX==dragDestinationX && dragSourceY==dragDestinationY && dragSourceXFine==dragDestinationXFine) {
      cursor=cursorDrag;
      selStart=cursorDrag;
      selEnd=cursorDrag;
    } else { // perform drag
      doDrag();
    }

    dragging=false;
  }

  // boundary check
  int chanCount=e->getTotalChannelCount();

  if (selStart.xCoarse<0) selStart.xCoarse=0;
  if (selStart.xCoarse>=chanCount) selStart.xCoarse=chanCount-1;
  if (selStart.y<0) selStart.y=0;
  if (selStart.y>=e->curSubSong->patLen) selStart.y=e->curSubSong->patLen-1;
  if (selEnd.xCoarse<0) selEnd.xCoarse=0;
  if (selEnd.xCoarse>=chanCount) selEnd.xCoarse=chanCount-1;
  if (selEnd.y<0) selEnd.y=0;
  if (selEnd.y>=e->curSubSong->patLen) selEnd.y=e->curSubSong->patLen-1;
  if (cursor.xCoarse<0) cursor.xCoarse=0;
  if (cursor.xCoarse>=chanCount) cursor.xCoarse=chanCount-1;
  if (cursor.y<0) cursor.y=0;
  if (cursor.y>=e->curSubSong->patLen) cursor.y=e->curSubSong->patLen-1;

  if (e->curSubSong->chanCollapse[selStart.xCoarse]==3) {
    selStart.xFine=0;
  }
  if (e->curSubSong->chanCollapse[selEnd.xCoarse] && selEnd.xFine>=(3-e->curSubSong->chanCollapse[selEnd.xCoarse])) {
    selEnd.xFine=2+e->curPat[selEnd.xCoarse].effectCols*2;
  }

  logV(_("finish selection: %d.%d,%d - %d.%d,%d"),selStart.xCoarse,selStart.xFine,selStart.y,selEnd.xCoarse,selEnd.xFine,selEnd.y);

  e->setMidiBaseChan(cursor.xCoarse);
}

void FurnaceGUI::moveCursor(int x, int y, bool select) {
  if (y>=editStepCoarse || y<=-editStepCoarse || x<=-5 || x>=5 ) {
    makeCursorUndo();
  }
  if (!select) {
    finishSelection();
  }

  DETERMINE_FIRST_LAST;
  
  curNibble=false;
  if (x!=0) {
    demandScrollX=true;
    if (x>0) {
      for (int i=0; i<x; i++) {
        if (++cursor.xFine>=(e->curSubSong->chanCollapse[cursor.xCoarse]?(4-e->curSubSong->chanCollapse[cursor.xCoarse]):(3+e->curPat[cursor.xCoarse].effectCols*2))) {
          cursor.xFine=0;
          if (++cursor.xCoarse>=lastChannel) {
            if (settings.wrapHorizontal!=0 && !select) {
              cursor.xCoarse=firstChannel;
              if (settings.wrapHorizontal==2) y++;
            } else {
              cursor.xCoarse=lastChannel-1;
              cursor.xFine=e->curSubSong->chanCollapse[cursor.xCoarse]?(3-e->curSubSong->chanCollapse[cursor.xCoarse]):(2+e->curPat[cursor.xCoarse].effectCols*2);
            }
          } else {
            while (!e->curSubSong->chanShow[cursor.xCoarse]) {
              cursor.xCoarse++;
              if (cursor.xCoarse>=e->getTotalChannelCount()) break;
            }
          }
        }
      }
    } else {
      for (int i=0; i<-x; i++) {
        if (--cursor.xFine<0) {
          if (--cursor.xCoarse<firstChannel) {
            if (settings.wrapHorizontal!=0 && !select) {
              cursor.xCoarse=lastChannel-1;
              cursor.xFine=2+e->curPat[cursor.xCoarse].effectCols*2;
              if (settings.wrapHorizontal==2) y--;
            } else {
              cursor.xCoarse=firstChannel;
              cursor.xFine=0;
            }
          } else {
            while (!e->curSubSong->chanShow[cursor.xCoarse]) {
              cursor.xCoarse--;
              if (cursor.xCoarse<0) break;
            }
            if (e->curSubSong->chanCollapse[cursor.xCoarse]) {
              cursor.xFine=3-e->curSubSong->chanCollapse[cursor.xCoarse];
            } else {
              cursor.xFine=2+e->curPat[cursor.xCoarse].effectCols*2;
            }
          }
        }
      }
    }
  }
  if (y!=0) {
    if (y>0) {
      for (int i=0; i<y; i++) {
        cursor.y++;
        if (cursor.y>=e->curSubSong->patLen) {
          if (settings.wrapVertical!=0 && !select) {
            cursor.y=0;
            if (settings.wrapVertical>1) {
              if (!e->isPlaying() || !followPattern) {
                if (curOrder<(e->curSubSong->ordersLen-1)) {
                  setOrder(curOrder+1);
                } else if (settings.wrapVertical==3) {
                  setOrder(0);
                } else {
                  cursor.y=e->curSubSong->patLen-1;
                }
              } else {
                cursor.y=e->curSubSong->patLen-1;
              }
            }
          } else {
            cursor.y=e->curSubSong->patLen-1;
          }
        }
      }
    } else {
      for (int i=0; i<-y; i++) {
        cursor.y--;
        if (cursor.y<0) {
          if (settings.wrapVertical!=0 && !select) {
            cursor.y=e->curSubSong->patLen-1;
            if (settings.wrapVertical>1) {
              if (!e->isPlaying() || !followPattern) {
                if (curOrder>0) {
                  setOrder(curOrder-1);
                } else if (settings.wrapVertical==3) {
                  setOrder(e->curSubSong->ordersLen-1);
                } else {
                  cursor.y=0;
                }
              } else {
                cursor.y=0;
              }
            }
          } else {
            cursor.y=0;
          }
        }
      }
    }
  }
  if (!select) {
    selStart=cursor;
  }
  selEnd=cursor;
  if (!settings.cursorMoveNoScroll) {
    updateScroll(cursor.y);
  }
  e->setMidiBaseChan(cursor.xCoarse);
}

void FurnaceGUI::moveCursorPrevChannel(bool overflow) {
  makeCursorUndo();
  finishSelection();
  curNibble=false;

  DETERMINE_FIRST_LAST;

  do {
    cursor.xCoarse--;
    if (cursor.xCoarse<0) break;
  } while (!e->curSubSong->chanShow[cursor.xCoarse]);
  if (cursor.xCoarse<firstChannel) {
    if (overflow) {
      cursor.xCoarse=lastChannel-1;
    } else {
      cursor.xCoarse=firstChannel;
    }
  }
  e->setMidiBaseChan(cursor.xCoarse);

  int xFineMax=(e->curSubSong->chanCollapse[cursor.xCoarse]?(4-e->curSubSong->chanCollapse[cursor.xCoarse]):(3+e->curPat[cursor.xCoarse].effectCols*2));
  if (cursor.xFine<0) cursor.xFine=0;
  if (cursor.xFine>=xFineMax) cursor.xFine=xFineMax-1;

  selStart=cursor;
  selEnd=cursor;
  demandScrollX=true;
}

void FurnaceGUI::moveCursorNextChannel(bool overflow) {
  makeCursorUndo();
  finishSelection();
  curNibble=false;

  DETERMINE_FIRST_LAST;

  do {
    cursor.xCoarse++;
    if (cursor.xCoarse>=e->getTotalChannelCount()) break;
  } while (!e->curSubSong->chanShow[cursor.xCoarse]);
  if (cursor.xCoarse>=lastChannel) {
    if (overflow) {
      cursor.xCoarse=firstChannel;
    } else {
      cursor.xCoarse=lastChannel-1;
    }
  }
  e->setMidiBaseChan(cursor.xCoarse);

  int xFineMax=(e->curSubSong->chanCollapse[cursor.xCoarse]?(4-e->curSubSong->chanCollapse[cursor.xCoarse]):(3+e->curPat[cursor.xCoarse].effectCols*2));
  if (cursor.xFine<0) cursor.xFine=0;
  if (cursor.xFine>=xFineMax) cursor.xFine=xFineMax-1;

  selStart=cursor;
  selEnd=cursor;
  demandScrollX=true;
}

void FurnaceGUI::moveCursorTop(bool select) {
  makeCursorUndo();
  if (!select) {
    finishSelection();
  }
  curNibble=false;
  if (cursor.y==0) {
    DETERMINE_FIRST;
    cursor.xCoarse=firstChannel;
    cursor.xFine=0;
    demandScrollX=true;
  } else {
    cursor.y=0;
  }
  if (!select) {
    selStart=cursor;
  }
  selEnd=cursor;
  e->setMidiBaseChan(cursor.xCoarse);
  updateScroll(cursor.y);
}

void FurnaceGUI::moveCursorBottom(bool select) {
  makeCursorUndo();
  if (!select) {
    finishSelection();
  }
  curNibble=false;
  if (cursor.y==e->curSubSong->patLen-1) {
    DETERMINE_LAST;
    cursor.xCoarse=lastChannel-1;
    if (cursor.xCoarse<0) cursor.xCoarse=0;
    cursor.xFine=2+e->curPat[cursor.xCoarse].effectCols*2;
    demandScrollX=true;
  } else {
    cursor.y=e->curSubSong->patLen-1;
  }
  if (!select) {
    selStart=cursor;
  }
  selEnd=cursor;
  e->setMidiBaseChan(cursor.xCoarse);
  updateScroll(cursor.y);
}

void FurnaceGUI::editAdvance() {
  finishSelection();
  cursor.y+=editStep;
  if (cursor.y>=e->curSubSong->patLen) cursor.y=e->curSubSong->patLen-1;
  selStart=cursor;
  selEnd=cursor;
  updateScroll(cursor.y);
}
