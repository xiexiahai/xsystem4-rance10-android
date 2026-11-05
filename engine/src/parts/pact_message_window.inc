/* First message-window schema. Validation precedes mutating the activity. */
static bool pact_mw_range(struct pact_reader *r,struct ex_tree *t,const char *key,float d,float lo,float hi,bool integer)
{
    float n=pact_num(r,t,key,d);
    if(n<lo || n>hi || (integer && n!=truncf(n))) r->error=true;
    return !r->error;
}
static bool pact_mw_list(struct pact_reader *r,struct ex_tree *t,const char *key,unsigned count,float lo,float hi)
{
    struct ex_value *v=pact_value(t,key);
    if(!v) return true;
    if(v->type!=EX_LIST || v->list->nr_items!=count) return false;
    for(unsigned i=0;i<count;i++) {
        float n=pact_at(r,t,key,i,0);
        if(n<lo || n>hi || n!=truncf(n)) return false;
    }
    return !r->error;
}
static bool pact_mw_validate(struct pact_reader *r,struct ex_tree *t)
{
    const char *names[]={"ＣＧ名","フラット名","テキスト"};
    for(unsigned i=0;i<3;i++) if(strlen(pact_string(r,t,names[i]))>1024*1024) return false;
    if(!pact_mw_range(r,t,"アクティブ",0,0,1,true) || !pact_mw_range(r,t,"テキスト位置",1,1,9,true) ||
       !pact_mw_range(r,t,"フォントタイプ",0,0,65535,true) || !pact_mw_range(r,t,"フォント尺寸",16,1,512,true) ||
       !pact_mw_range(r,t,"フォント太さ",0,0,16,false) || !pact_mw_range(r,t,"フォント縁取り",0,0,16,false) ||
       !pact_mw_range(r,t,"字速度",23,0,60000,true) || !pact_mw_range(r,t,"文字間隔",0,-512,512,true) ||
       !pact_mw_range(r,t,"行間隔",0,-512,512,true) || !pact_mw_range(r,t,"已读判定",0,0,1,true)) return false;
    if(!pact_mw_list(r,t,"非アクティブ時の乘法颜色",3,0,255) || !pact_mw_list(r,t,"フォント色",3,0,255) ||
       !pact_mw_list(r,t,"フォント縁取り色",3,0,255) || !pact_mw_list(r,t,"文本エリア",4,0,4096)) return false;
    struct ex_tree *mark=pact_branch(t,"キー待ちマーク");
    if(mark) {
        if(strlen(pact_string(r,mark,"ＣＧ名"))>1024*1024 || strlen(pact_string(r,mark,"フラット名"))>1024*1024 ||
           !pact_mw_list(r,mark,"座標",3,-1000000,1000000) ||
           !pact_mw_range(r,mark,"循环ＣＧ开始编号",0,0,1000000,true) ||
           !pact_mw_range(r,mark,"循环ＣＧ张数",0,0,4096,true) ||
           !pact_mw_range(r,mark,"循环ＣＧ切り替え时间",0,0,1000000,true)) return false;
        if(pact_num(r,mark,"循环ＣＧ张数",0)>0 && pact_num(r,mark,"循环ＣＧ切り替え时间",0)<1) return false;
    }
    return !r->error;
}
static bool pact_mw_image(struct pact_reader *r,int no,struct ex_tree *t,bool mark)
{
    struct string *cg=pact_to_game(pact_string(r,t,"ＣＧ名")),*flat=pact_to_game(pact_string(r,t,"フラット名"));
    bool ok=parts_message_window_set_image(no,mark,false,cg,pact_num(r,t,"循环ＣＧ开始编号",0),
      pact_num(r,t,"循环ＣＧ张数",0),pact_num(r,t,"循环ＣＧ切り替え时间",0));
    if(ok) ok=parts_message_window_set_image(no,mark,true,flat,0,0,0);
    free_string(cg);free_string(flat);return ok && !r->error;
}
static bool pact_mw_apply(struct pact_reader *r,int no,struct ex_tree *t,int state)
{
    if(state!=1 || !pact_mw_validate(r,t)) return false;
    PE_SetComponentType(no,19,1);
    PE_SetMessageWindowTextFont(no,pact_num(r,t,"フォントタイプ",0),pact_num(r,t,"フォント尺寸",16),
      pact_at(r,t,"フォント色",0,255),pact_at(r,t,"フォント色",1,255),pact_at(r,t,"フォント色",2,255),pact_num(r,t,"フォント太さ",0),
      pact_at(r,t,"フォント縁取り色",0,0),pact_at(r,t,"フォント縁取り色",1,0),pact_at(r,t,"フォント縁取り色",2,0),pact_num(r,t,"フォント縁取り",0));
    PE_SetMessageWindowTextArea(no,pact_at(r,t,"文本エリア",0,0),pact_at(r,t,"文本エリア",1,0),pact_at(r,t,"文本エリア",2,0),pact_at(r,t,"文本エリア",3,0));
    PE_SetMessageWindowTextOriginPosMode(no,pact_num(r,t,"テキスト位置",1));
    PE_SetMessageWindowTextSpeed(no,pact_num(r,t,"字速度",23));
    PE_SetMessageWindowTextSpace(no,pact_num(r,t,"文字間隔",0),pact_num(r,t,"行間隔",0));
    PE_SetMessageWindowActive(no,pact_num(r,t,"アクティブ",0));
    PE_SetMessageWindowInactiveMultipleColor(no,pact_at(r,t,"非アクティブ時の乘法颜色",0,255),pact_at(r,t,"非アクティブ時の乘法颜色",1,255),pact_at(r,t,"非アクティブ時の乘法颜色",2,255));
    if(!pact_mw_image(r,no,t,false)) return false;
    struct ex_tree *mark=pact_branch(t,"キー待ちマーク");
    if(mark) {
        if(!pact_mw_image(r,no,mark,true)) return false;
        PE_SetKeyWaitPos(no,pact_at(r,mark,"座標",0,0),pact_at(r,mark,"座標",1,0),pact_at(r,mark,"座標",2,0));
    }
    struct string *text=pact_to_game(pact_string(r,t,"テキスト"));
    bool ok=parts_message_window_set_text(no,text,pact_num(r,t,"消息编号",-1),&EMPTY_STRING,0,0);
    free_string(text);return ok && !r->error;
}
