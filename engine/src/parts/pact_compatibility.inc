/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Narrow project compatibility rule, not a general interpretation of editor
 * visibility. Keep the duplicate node registered so script name lookups work.
 * A changed template fails the fingerprint and retains the ordinary policy. */
static const struct ex_tree *pact_compat_new_backup(struct string *file, struct ex_tree *root)
{
	if (!file || !root) return NULL;
	const char *expected = "Game/Component/NewMarkView";
	size_t len = strlen(expected);
	if ((size_t)file->size < len) return NULL;
	for (size_t i = 0; i < len; i++) {
		char c = file->text[i] == '\\' ? '/' : file->text[i];
		if (c != expected[i]) return NULL;
	}
	if ((size_t)file->size != len &&
			!((size_t)file->size == len + 7 && !memcmp(file->text + len,".pactex",7)))
		return NULL;
	struct ex_tree *children = pact_branch(root,"子パーツ");
	if (children && children->nr_children == 1)
		children = pact_branch(pact_branch(children,"Base"),"子パーツ");
	if (!children || children->nr_children != 2) return NULL;
	struct ex_tree *live = pact_branch(children,"Flat");
	struct ex_tree *backup = pact_branch(children,"Flat:0");
	if (!live || !backup) return NULL;
	struct pact_reader check = {0};
	struct ex_tree *nodes[] = {live,backup};
	const int flags[][4] = {{1,0,1,255},{0,1,1,128}};
	const int positions[][3] = {{25,9,1},{0,0,2}};
	const char *keys[] = {"エディタ上表示","ロックしている","表示","アルファ"};
	const char *flat_names[2];
	for (unsigned i = 0; i < 2; i++) {
		for (unsigned j = 0; j < 4; j++)
			if (pact_num(&check,nodes[i],keys[j],-1) != flags[i][j]) return NULL;
		for (unsigned j = 0; j < 3; j++)
			if (pact_at(&check,nodes[i],"座標",j,-9999) != positions[i][j]) return NULL;
		struct ex_tree *component = pact_branch(nodes[i],"種類別情報");
		if (pact_num(&check,component,"パーツタイプ",-1) != 10) return NULL;
		struct ex_tree *state = pact_branch(component,"通常状態");
		if (pact_num(&check,state,"パーツタイプ",-1) != 20) return NULL;
		flat_names[i] = pact_string(&check,state,"フラット名");
	}
	if (check.error || !*flat_names[0] || strcmp(flat_names[0],flat_names[1])) return NULL;
	return backup;
}

/* The shipped quest-clear template retains an inactive route-progress mockup
 * (99999). First-hand PC quest-clear captures show EXP and rewards instead.
 * This is deliberately scoped to this template/fingerprint, not a global
 * interpretation of editor visibility, nor a replacement progress value. */
static bool pact_compat_unused_route(struct string *file,struct ex_tree *root,
        const struct ex_tree *out[3])
{
    for(int i=0;i<3;i++)out[i]=NULL;
    if(!file || !root)return false;
    const char *expected="Game/Quest/QuestResultView02";size_t len=strlen(expected);
    if((size_t)file->size!=len && (size_t)file->size!=len+7)return false;
    for(size_t i=0;i<len;i++)if((file->text[i]=='\\'?'/':file->text[i])!=expected[i])return false;
    if((size_t)file->size>len && memcmp(file->text+len,".pactex",7))return false;
    struct ex_tree *children=pact_branch(root,"子パーツ");if(!children)return false;
    struct pact_reader check={0};const char *names[]={"CaptionRoute","RouteBase","RouteNumber"};
    const struct ex_tree *found[3];
    for(int i=0;i<3;i++) {
        struct ex_tree *t=pact_branch(children,names[i]);if(!t)return false;
        if(pact_num(&check,t,"表示",-1)!=1 || pact_num(&check,t,"エディタ上表示",-1)!=0 ||
            pact_num(&check,t,"アルファ",-1)!=255)return false;
        struct ex_tree *component=pact_branch(t,"種類別情報");
        if(pact_num(&check,component,"パーツタイプ",-1)!=10)return false;
        struct ex_tree *state=pact_branch(component,"通常状態");
        if(pact_num(&check,state,"パーツタイプ",-1)!=(i==2?16:11))return false;
        if(i==2 && pact_num(&check,state,"数値",-1)!=99999)return false;
        found[i]=t;
    }
    const char *live[]={"CaptionExp","ExpNumber"};
    for(int i=0;i<2;i++) {
        struct ex_tree *t=pact_branch(children,live[i]);if(!t || pact_num(&check,t,"エディタ上表示",-1)!=1)return false;
    }
    if(check.error)return false;
    for(int i=0;i<3;i++)out[i]=found[i];
    return true;
}

/* User-confirmed extra HQ map reference. This exact-template policy does not
 * establish the original DLL meaning of editor/lock flags. Keep registration,
 * CG contents, transforms and editor mode intact; changed templates fail closed. */
static const struct ex_tree *pact_compat_quest_background_reference(struct string *file,struct ex_tree *root)
{
    if(!file || !root)return NULL;
    const char *expected="Game/QuestSelect/QuestSelectBackGround";size_t len=strlen(expected);
    if((size_t)file->size!=len && (size_t)file->size!=len+7)return NULL;
    for(size_t i=0;i<len;i++)if((file->text[i]=='\\'?'/':file->text[i])!=expected[i])return NULL;
    if((size_t)file->size>len && memcmp(file->text+len,".pactex",7))return NULL;
    struct ex_tree *children=pact_branch(root,"子パーツ");
    if(!children || children->nr_children!=3)return NULL;
    struct pact_reader check={0};
    const char *names[]={"QuestList","Default","ＣＧパーツ_000:0"};
    const char *keys[]={"エディタ上表示","ロックしている","表示","アルファ"};
    const int flags[3][4]={{1,0,1,255},{1,0,1,255},{0,1,1,128}};
    const float pos[3][3]={{960,540,1},{960,540,2},{902,419,3}};
    const struct ex_tree *reference=NULL;const char *cg[3];
    for(int i=0;i<3;i++) {
        struct ex_tree *t=pact_branch(children,names[i]);if(!t)return NULL;
        for(int j=0;j<4;j++)if(pact_num(&check,t,keys[j],-1)!=flags[i][j])return NULL;
        for(int j=0;j<3;j++) {
            if(pact_at(&check,t,"座標",j,-9999)!=pos[i][j])return NULL;
            if(pact_at(&check,t,"回転",j,-9999)!=(i==2?(j==0?-40.f:j==2?-5.f:0.f):0.f))return NULL;
        }
        for(int j=0;j<2;j++)if(pact_at(&check,t,"拡大縮小",j,-9999)!=(i==2?.67f:1.f))return NULL;
        if(pact_num(&check,t,"原点座標モード",-1)!=5)return NULL;
        struct ex_tree *component=pact_branch(t,"種類別情報");
        if(pact_num(&check,component,"パーツタイプ",-1)!=10)return NULL;
        struct ex_tree *state=pact_branch(component,"通常状態");
        if(pact_num(&check,state,"パーツタイプ",-1)!=11)return NULL;
        cg[i]=pact_string(&check,state,"ＣＧ名");
        if(i==2)reference=t;
    }
    if(check.error || strcmp(cg[0],"シス／任务一览／背景") ||
       strcmp(cg[1],"シス／准备／地图") || strcmp(cg[1],cg[2]))return NULL;
    return reference;
}

