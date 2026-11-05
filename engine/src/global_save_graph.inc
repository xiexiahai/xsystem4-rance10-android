/* SPDX-License-Identifier: GPL-2.0-or-later
 * Project R10GS1 global groups, separate from GSAVE and whole-VM R10VM1.
 * Include from savedata.c; enable writing only with the explicit test flag. */
#include "global_graph_json.h"
#include "global_graph_install.h"
#include "global_graph_file.h"
#include "global_graph_program.h"
#include "g12_comment_program_cache.h"
static cJSON *g12_hex_text(const char *text)
{
    if(!text)return NULL;size_t n=strlen(text);if(n>G12_STRING_LIMIT)return NULL;
    char *hex=malloc(n*2+1);if(!hex)return NULL;static const char digits[]="0123456789abcdef";
    for(size_t i=0;i<n;i++){unsigned c=(unsigned char)text[i];hex[i*2]=digits[c>>4];hex[i*2+1]=digits[c&15];}
    hex[n*2]=0;cJSON *j=cJSON_CreateString(hex);free(hex);return j;
}
static bool g12_match_text(const cJSON *j,const char *text)
{
    if(!cJSON_IsString(j) || !j->valuestring || !text)return false;
    size_t n=strlen(text);if(n>G12_STRING_LIMIT || strlen(j->valuestring)!=n*2)return false;
    for(size_t i=0;i<n;i++) {
        int a=g12_unhex_char(j->valuestring[i*2]),b=g12_unhex_char(j->valuestring[i*2+1]);
        if(a<0 || b<0 || (unsigned char)text[i]!=(unsigned)((a<<4)|b))return false;
    }
    return true;
}
static bool g12_payload_keys(const cJSON *payload)
{
    const char *keys[]={"format","program","key","group","all","roots","nodes","comment"};
    return g12_keys(payload,keys,cJSON_GetObjectItemCaseSensitive(payload,"comment")?8:7);
}
static bool g12_comment_base(const cJSON *payload,const char *key)
{
    if(!key || !g12_payload_keys(payload))return false;
    const cJSON *format=cJSON_GetObjectItemCaseSensitive(payload,"format"),
        *program=cJSON_GetObjectItemCaseSensitive(payload,"program");
    char expected[64];
    return g12_comment_program_id(ain,expected) && cJSON_IsString(format) && !strcmp(format->valuestring,"R10GS1") &&
        cJSON_IsString(program) && !strcmp(program->valuestring,expected) &&
        g12_match_text(cJSON_GetObjectItemCaseSensitive(payload,"key"),key) &&
        cJSON_IsString(cJSON_GetObjectItemCaseSensitive(payload,"group")) &&
        cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(payload,"all")) &&
        cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(payload,"roots")) &&
        cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(payload,"nodes"));
}
bool write_group_save_comment(const char *key,const char *filename,const struct string *comment)
{
    if(!comment || comment->size<0 || comment->size>65536)return false;
    uint8_t *raw=NULL;size_t size=0;cJSON *payload=NULL,*node=NULL,*hex=NULL;char *text=NULL;bool ok=false;
    if(!g12_read_envelope(filename,&raw,&size) || !(payload=g12_parse_json(raw,size)) || !g12_comment_base(payload,key))goto out;
    node=g12_json_string(comment);if(!node)goto out;
    hex=cJSON_DetachItemFromObjectCaseSensitive(node,"bytes");if(!hex)goto out;
    cJSON_DeleteItemFromObjectCaseSensitive(payload,"comment");
    bool attached=g12_json_put(payload,"comment",hex);hex=NULL;if(!attached)goto out;
    text=cJSON_PrintUnformatted(payload);if(!text)goto out;
    ok=g12_write_envelope(filename,(const uint8_t*)text,strlen(text));
out:free(raw);cJSON_Delete(payload);cJSON_Delete(node);cJSON_Delete(hex);cJSON_free(text);return ok;
}
bool read_group_save_comment(const char *key,const char *filename,struct string **comment)
{
    if(!comment)return false;uint8_t *raw=NULL;size_t size=0;cJSON *payload=NULL;struct string *next=NULL;bool ok=false;
    if(!g12_read_envelope(filename,&raw,&size) || !(payload=g12_parse_json(raw,size)) || !g12_comment_base(payload,key))goto out;
    const cJSON *hex=cJSON_GetObjectItemCaseSensitive(payload,"comment");
    if(!cJSON_IsString(hex) || !hex->valuestring || strlen(hex->valuestring)>131072 || !(next=g12_unhex(hex)))goto out;
    if(*comment)free_string(*comment);*comment=next;next=NULL;ok=true;
out:free(raw);cJSON_Delete(payload);if(next)free_string(next);return ok;
}
static bool g12_pick_globals(const char *group,int **ids,size_t *count)
{
    *ids=NULL;*count=0;if(!ain || ain->nr_globals<0 || (unsigned)ain->nr_globals>G12_NODE_LIMIT)return false;
    int selected=-1;
    if(group) {
        for(int i=0;i<ain->nr_global_groups;i++)if(!strcmp(group,ain->global_group_names[i]?ain->global_group_names[i]:"")){selected=i;break;}
        if(selected<0)return false;
    }
    int *values=malloc((ain->nr_globals?ain->nr_globals:1)*sizeof(int));if(!values)return false;
    for(int i=0;i<ain->nr_globals;i++)if(!group || ain->globals[i].group_index==selected)values[(*count)++]=i;
    *ids=values;return true;
}
static bool g12_is_file(const char *filename)
{
    if(!filename || !*filename)return false;
    char *path=savedir_path(filename);FILE *f=file_open_utf8(path,"rb");free(path);if(!f)return false;
    uint8_t prefix[8];size_t n=fread(prefix,1,8,f);bool ok=!ferror(f);if(fclose(f))ok=false;
    return ok && n && !memcmp(prefix,g12_magic,n);
}
// Local diagnostics only; no resource data or object contents are dumped.
static void g12_external_diagnostic(const struct g12_graph *graph,const char *group)
{
    if(!graph->stage || strcmp(graph->stage,"external-weak"))return;
    int target=graph->problem_slot;
    if(target<=0 || (size_t)target>=heap_size || heap[target].type!=VM_PAGE || !heap[target].page)return;
    struct page *p=heap[target].page;
    WARNING("R10GS1 external receiver: group=%s slot=%d page=%d index=%d refs=%d",
        group?group:"<all>",target,p->type,p->index,heap[target].ref);
    if(p->type==STRUCT_PAGE && p->index>=0 && p->index<ain->nr_structures)
        WARNING("R10GS1 receiver structure: %s",display_sjis0(ain->structures[p->index].name));
    int queue[32]={target},head=0,tail=1;
    while(head<tail && tail<32) {
        int child=queue[head++];
        for(size_t i=0;i<heap_size && tail<32;i++) {
            if(heap[i].ref<1 || heap[i].type!=VM_PAGE || !heap[i].page)continue;
            struct page *owner=heap[i].page;
            for(int k=0;k<owner->nr_vars && tail<32;k++) {
                bool owned=owner->type==DELEGATE_PAGE ? k%3==0 && delegate_get_environment(owner,k/3)==child :
                    owner->values[k].i==child && g12_owns_word(variable_type(owner,k,NULL,NULL));
                if(!owned)continue;
                int expected=-1;enum ain_data_type declared=owner->type==DELEGATE_PAGE?AIN_VOID:variable_type(owner,k,&expected,NULL);
                WARNING("R10GS1 ancestry: child=%d owner=%zu page=%d index=%d word=%d global-group=%d",
                    child,i,owner->type,owner->index,k,owner->type==GLOBAL_PAGE?ain->globals[k].group_index:-1);
                WARNING("R10GS1 ancestry type: declared=%d expected-structure=%d actual-structure=%d",
                    declared,expected,heap[child].type==VM_PAGE && heap[child].page?heap[child].page->index:-1);
                bool seen=false;for(int j=0;j<tail;j++)if(queue[j]==(int)i)seen=true;
                if(!seen && i>0)queue[tail++]=i;
            }
        }
    }
    for(size_t i=0;i<vm_active_frame_count();i++) {
        int slot=vm_active_frame_page(i);
        if(slot>0 && heap_index_valid(slot) && heap[slot].type==VM_PAGE && heap[slot].page)
            WARNING("R10GS1 active frame: position=%zu slot=%d function=%d vars=%d",i,slot,heap[slot].page->index,heap[slot].page->nr_vars);
    }
    int shown=0;
    for(size_t i=0;i<heap_size && shown<12;i++) {
        if(heap[i].ref<1 || heap[i].type!=VM_PAGE || !heap[i].page)continue;
        struct page *owner=heap[i].page;if(owner->type==DELEGATE_PAGE)continue;
        for(int k=0;k<owner->nr_vars && shown<12;k++) {
            enum ain_data_type type=variable_type(owner,k,NULL,NULL);
            if(owner->values[k].i==target && g12_owns_word(type)) {
                WARNING("R10GS1 receiver owner: slot=%zu page=%d index=%d word=%d type=%d selected=%d global-group=%d",
                    i,owner->type,owner->index,k,type,graph->ids[i]>=0,
                    owner->type==GLOBAL_PAGE?ain->globals[k].group_index:-1);shown++;
            }
        }
    }
}
static int g12_save_globals(const char *key,const char *filename,const char *group,int *n_out)
{
    if(n_out)*n_out=0;
    if(!key || !filename || !*filename || !page_index_valid(0) || !heap[0].page ||
       heap[0].page->type!=GLOBAL_PAGE || heap[0].page->nr_vars!=ain->nr_globals)return 0;
    int *ids=NULL;size_t count=0;struct g12_root *roots=NULL,*encoded=NULL;
    const struct ain_type **types=NULL;struct g12_graph graph={0};struct g12_candidate candidate={0};
    int *external=NULL,binding_slots[G12_EXTERNAL_LIMIT];bool *allowed=NULL;
    struct g12_binding *bindings=NULL;size_t nr_bindings=0;
    cJSON *payload=NULL,*nodes=NULL,*rows=NULL;char *json=NULL;bool ok=false;const char *stage="selection";
    if(!g12_pick_globals(group,&ids,&count))goto out;
    roots=malloc((count?count:1)*sizeof(*roots));encoded=malloc((count?count:1)*sizeof(*encoded));
    types=malloc((count?count:1)*sizeof(*types));if(!roots || !encoded || !types)goto out;
    external=calloc(heap_size,sizeof(int));allowed=malloc((ain->nr_globals?ain->nr_globals:1)*sizeof(bool));
    bindings=calloc(G12_EXTERNAL_LIMIT,sizeof(*bindings));if(!external || !allowed || !bindings)goto out;
    for(int i=0;i<ain->nr_globals;i++)allowed[i]=true;
    for(size_t i=0;i<count;i++)allowed[ids[i]]=false;
    for(size_t i=0;i<count;i++){types[i]=&ain->globals[ids[i]].type;roots[i]=(struct g12_root){types[i]->data,global_get(ids[i]).i};}
    stage="collect";
    while(!g12_collect_view_external(&graph,heap,heap_size,roots,count,external)) {
        int slot=graph.problem_slot;
        if(!graph.stage || strcmp(graph.stage,"external-weak") || nr_bindings==G12_EXTERNAL_LIMIT ||
           slot<=0 || (size_t)slot>=heap_size || external[slot] ||
           !g12_find_external_binding(slot,allowed,&bindings[nr_bindings]))goto out;
        binding_slots[nr_bindings++]=slot;external[slot]=slot;g12_graph_free(&graph);
    }
    stage="nodes";if(!(nodes=g12_nodes_encode_bound(&graph,bindings,binding_slots,nr_bindings)))goto out;
    for(size_t i=0;i<count;i++) {
        encoded[i].type=roots[i].type;
        if(!g12_word_encode(&graph,roots[i].type,roots[i].value,&encoded[i].value))goto out;
    }
    stage="validate-candidate";
    if(!g12_nodes_decode_bound(nodes,&candidate,allowed) || !g12_candidate_validate(&candidate,encoded,count,types))goto out;
    g12_candidate_free(&candidate);
    stage="program";char program[64];if(!g12_program_id(ain,program))goto out;
    payload=cJSON_CreateObject();rows=cJSON_CreateArray();if(!payload || !rows)goto out;
    stage="metadata";
    if(!g12_json_put(payload,"format",cJSON_CreateString("R10GS1")) ||
       !g12_json_put(payload,"program",cJSON_CreateString(program)) ||
       !g12_json_put(payload,"key",g12_hex_text(key)) ||
       !g12_json_put(payload,"group",g12_hex_text(group?group:"")) ||
       !g12_json_put(payload,"all",cJSON_CreateBool(!group)))goto out;
    cJSON *tail=NULL;
    for(size_t i=0;i<count;i++) {
        cJSON *row=cJSON_CreateArray(),*last=NULL;if(!row)goto out;
        if(!g12_json_add_num(row,&last,ids[i]) || !g12_json_add_num(row,&last,encoded[i].value) ||
           !g12_json_add(rows,&tail,row)){cJSON_Delete(row);goto out;}
    }
    bool attached=g12_json_put(payload,"roots",rows);rows=NULL;if(!attached)goto out;
    attached=g12_json_put(payload,"nodes",nodes);nodes=NULL;if(!attached)goto out;
    stage="file";json=cJSON_PrintUnformatted(payload);if(!json)goto out;
    ok=g12_write_envelope(filename,(const uint8_t*)json,strlen(json));
    if(ok)NOTICE("[global-graph] saved globals=%zu nodes=%zu bound-receivers=%zu",count,graph.count,nr_bindings);
out:
    if(!ok)g12_external_diagnostic(&graph,group);
    if(!ok)WARNING("R10GS1 save rejected: %s (%s slot=%d type=%d value=%d)",stage,
        graph.stage?graph.stage:"not-collected",graph.problem_slot,graph.problem_type,graph.problem_value);
    if(ok && n_out)*n_out=count;
    free(ids);free(roots);free(encoded);free(types);free(external);free(allowed);free(bindings);
    g12_graph_free(&graph);g12_candidate_free(&candidate);
    cJSON_Delete(payload);cJSON_Delete(nodes);cJSON_Delete(rows);cJSON_free(json);return ok;
}
static int g12_load_globals(const char *key,const char *filename,const char *group,int *n_out)
{
    if(n_out)*n_out=0;if(!key)return 0;
    uint8_t *raw=NULL;size_t size=0,count=0;int *ids=NULL;
    struct g12_root *roots=NULL;const struct ain_type **types=NULL;
    struct g12_candidate candidate={0};cJSON *payload=NULL;bool ok=false;const char *stage="file";
    bool *allowed=NULL;
    if(!g12_read_envelope(filename,&raw,&size))goto out;
    stage="json";payload=g12_parse_json(raw,size);if(!payload)goto out;
    if(!g12_payload_keys(payload))goto out;
    const cJSON *comment=cJSON_GetObjectItemCaseSensitive(payload,"comment");
    if(comment) {
        if(!cJSON_IsString(comment) || !comment->valuestring || strlen(comment->valuestring)>131072)goto out;
        struct string *check=g12_unhex(comment);if(!check)goto out;free_string(check);
    }
    const cJSON *format=cJSON_GetObjectItemCaseSensitive(payload,"format"),
        *program=cJSON_GetObjectItemCaseSensitive(payload,"program"),
        *all=cJSON_GetObjectItemCaseSensitive(payload,"all"),
        *rows=cJSON_GetObjectItemCaseSensitive(payload,"roots"),
        *nodes=cJSON_GetObjectItemCaseSensitive(payload,"nodes");
    char expected[64];stage="metadata";
    if(!g12_program_id(ain,expected) || !cJSON_IsString(format) || strcmp(format->valuestring,"R10GS1") ||
       !cJSON_IsString(program) || strcmp(program->valuestring,expected) ||
       !g12_match_text(cJSON_GetObjectItemCaseSensitive(payload,"key"),key) ||
       !g12_match_text(cJSON_GetObjectItemCaseSensitive(payload,"group"),group?group:"") ||
       !cJSON_IsBool(all) || cJSON_IsTrue(all)!=(group==NULL) || !cJSON_IsArray(rows) ||
       !g12_pick_globals(group,&ids,&count) || cJSON_GetArraySize(rows)!=(int)count)goto out;
    roots=malloc((count?count:1)*sizeof(*roots));types=malloc((count?count:1)*sizeof(*types));
    allowed=malloc((ain->nr_globals?ain->nr_globals:1)*sizeof(bool));if(!roots || !types || !allowed)goto out;
    for(int i=0;i<ain->nr_globals;i++)allowed[i]=true;
    for(size_t i=0;i<count;i++)allowed[ids[i]]=false;
    size_t i=0;const cJSON *row;
    cJSON_ArrayForEach(row,rows) {
        int id,value;if(!cJSON_IsArray(row) || cJSON_GetArraySize(row)!=2 ||
           !g12_int(cJSON_GetArrayItem(row,0),&id) || id!=ids[i] ||
           !g12_int(cJSON_GetArrayItem(row,1),&value))goto out;
        types[i]=&ain->globals[id].type;roots[i]=(struct g12_root){types[i]->data,value};i++;
    }
    stage="candidate";
    const char *mode=getenv("XSYSTEM4_AIN12_GLOBAL_GRAPH");
    /* A controller frame can end before either a reset or a cold start.
     * The decoder proves the complete static path and refuses live or
     * ambiguous matching owners before expiring a frame-only weak receiver. */
    bool expire_frames=mode && (!strcmp(mode,"1") || !strcmp(mode,"fallback"));
    if(!g12_nodes_decode_bound_policy(nodes,&candidate,allowed,expire_frames) || !g12_candidate_validate(&candidate,roots,count,types))goto out;
    stage="install";ok=g12_install(&candidate,ids,roots,count);
    if(ok) {
        size_t expired=0;for(size_t i=1;i<candidate.capacity;i++)expired+=candidate.external[i]<0;
        NOTICE("[global-graph] loaded globals=%zu nodes=%zu bound-receivers=%zu expired-receivers=%zu",count,candidate.capacity-1,candidate.nr_bindings-expired,expired);
    }
out:
    if(!ok)WARNING("R10GS1 load rejected: file=%s stage=%s; current VM retained",filename,stage);
    if(ok && n_out)*n_out=count;
    free(raw);free(ids);free(roots);free(types);free(allowed);cJSON_Delete(payload);g12_candidate_free(&candidate);return ok;
}
