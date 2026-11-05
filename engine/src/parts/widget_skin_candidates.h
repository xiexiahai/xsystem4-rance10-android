/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PARTS_WIDGET_SKIN_CANDIDATES_H
#define PARTS_WIDGET_SKIN_CANDIDATES_H

#include <stdbool.h>
#include <stddef.h>

struct widget_skin_candidate {
    bool checked;
    int state;
    bool localized;
};

static inline size_t widget_skin_candidates(bool checked,int state,
        struct widget_skin_candidate out[8])
{
    if(!out || state<0 || state>3)return 0;
    size_t n=0;
    const bool states[2]={true,false};
    if(checked) {
        for(int c=0;c<2;c++) {
            out[n++]=(struct widget_skin_candidate){.checked=states[c],
                .state=state,.localized=true};
            out[n++]=(struct widget_skin_candidate){.checked=states[c],
                .state=state,.localized=false};
            if(state!=0) {
                out[n++]=(struct widget_skin_candidate){.checked=states[c],
                    .state=0,.localized=true};
                out[n++]=(struct widget_skin_candidate){.checked=states[c],
                    .state=0,.localized=false};
            }
        }
    } else {
        out[n++]=(struct widget_skin_candidate){.checked=false,
            .state=state,.localized=true};
        out[n++]=(struct widget_skin_candidate){.checked=false,
            .state=state,.localized=false};
        if(state!=0) {
            out[n++]=(struct widget_skin_candidate){.checked=false,
                .state=0,.localized=true};
            out[n++]=(struct widget_skin_candidate){.checked=false,
                .state=0,.localized=false};
        }
    }
    return n;
}

#endif /* PARTS_WIDGET_SKIN_CANDIDATES_H */
