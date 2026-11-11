/* AIN12 queries return values and accept a default plus reader ID.
 * Reader 0 is the main EX file. Additional readers are not implemented. */
static struct ex_value *MainEX12_value(struct string *name,int id)
{
	if(id!=0) VM_ERROR("Additional EX reader %d is not implemented",id);
	struct ex_value *v=ex_get(ex,name->text);
	if(getenv("XSYSTEM4_TRACE_INPUT"))
		sys_message("[ex12] value name=%.48s found=%d type=%d\n",
			name&&name->text?name->text:"",v?1:0,v?v->type:-1);
	return v;
}
static struct ex_value *MainEX12_at(struct string *name,int index,int id)
{
	struct ex_value *v=MainEX12_value(name,id);
	struct ex_value *r=v && v->type==EX_LIST && index>=0 ? ex_list_get(v->list,index):NULL;
	if(getenv("XSYSTEM4_TRACE_INPUT"))
		sys_message("[ex12] at name=%.48s idx=%d found=%d type=%d\n",
			name&&name->text?name->text:"",index,r?1:0,r?r->type:-1);
	return r;
}
static struct ex_value *MainEX12_at2(struct string *name,int row,int col,int id)
{
	struct ex_value *v=MainEX12_value(name,id);
	struct ex_value *r=v && v->type==EX_TABLE && row>=0 && col>=0 ? ex_table_get(v->t,row,col):NULL;
	if(getenv("XSYSTEM4_TRACE_INPUT")) {
		char b2[72]={0};
		if(r&&r->type==EX_STRING&&r->s&&r->s->text) snprintf(b2,sizeof(b2),"%.60s",r->s->text);
		sys_message("[ex12] at2 name=%.40s row=%d col=%d found=%d type=%d val=%s\n",
			name&&name->text?name->text:"",row,col,r?1:0,r?r->type:-1,b2);
	}
	return r;
}
#define EX12_VALUES(PREFIX,PARAMS,GET) \
static int PREFIX##Int PARAMS(int dflt,int id) {struct ex_value *v=GET;int r=v && v->type==EX_INT?v->i:dflt; \
	if(getenv("XSYSTEM4_TRACE_INPUT")) sys_message("[ex12] Int dflt=%d result=%d used_default=%d\n",dflt,r,!(v&&v->type==EX_INT)); return r;} \
static float PREFIX##Float PARAMS(float dflt,int id) {struct ex_value *v=GET;float r=v && v->type==EX_FLOAT?v->f:dflt; \
	if(getenv("XSYSTEM4_TRACE_INPUT")) sys_message("[ex12] Float dflt=%f result=%f used_default=%d\n",(double)dflt,(double)r,!(v&&v->type==EX_FLOAT)); return r;} \
static struct string *PREFIX##String PARAMS(struct string *dflt,int id) {struct ex_value *v=GET; \
	if(getenv("XSYSTEM4_TRACE_INPUT")) { char b2[72]={0}; if(v&&v->type==EX_STRING&&v->s&&v->s->text) snprintf(b2,sizeof(b2),"%.60s",v->s->text); \
	sys_message("[ex12] String used_default=%d val=%s\n",!(v&&v->type==EX_STRING),b2);} \
	return string_ref(v && v->type==EX_STRING?v->s:dflt);}
// Parameter-list helper macros keep all nine signatures explicit to libffi.
#define EX12_TOP(T,I) (struct string *name,T,I)
#define EX12_ONE(T,I) (struct string *name,int index,T,I)
#define EX12_TWO(T,I) (struct string *name,int row,int col,T,I)
EX12_VALUES(MainEX12_,EX12_TOP,MainEX12_value(name,id))
EX12_VALUES(MainEX12_A,EX12_ONE,MainEX12_at(name,index,id))
EX12_VALUES(MainEX12_A2,EX12_TWO,MainEX12_at2(name,row,col,id))
#undef EX12_VALUES
#undef EX12_TOP
#undef EX12_ONE
#undef EX12_TWO
static int MainEX12_Row(struct string *name,int id) {MainEX12_value(name,id);return MainEXFile_name_Row(name);}
static int MainEX12_Col(struct string *name,int id) {MainEX12_value(name,id);return MainEXFile_name_Col(name);}
static int MainEX12_Type(struct string *name,int id) {struct ex_value *v=MainEX12_value(name,id);return v?v->type:0;}
static bool MainEX12_Exists(struct string *name,int id) {return !!MainEX12_value(name,id);}
static bool MainEX12_AExists(struct string *name,int index,int id) {return !!MainEX12_at(name,index,id);}
static bool MainEX12_A2Exists(struct string *name,int row,int col,int id) {return !!MainEX12_at2(name,row,col,id);}
static int MainEX12_AType(struct string *name,int index,int id) {struct ex_value *v=MainEX12_at(name,index,id);return v?v->type:0;}
static int MainEX12_A2Type(struct string *name,int row,int col,int id) {struct ex_value *v=MainEX12_at2(name,row,col,id);return v?v->type:0;}
static int MainEX12_GetRowAtIntKey(struct string *name,int key,int id) {MainEX12_value(name,id);return MainEXFile_name_GetRowAtIntKey(name,key);}
static int MainEX12_GetRowAtStringKey(struct string *name,struct string *key,int id) {MainEX12_value(name,id);return MainEXFile_name_GetRowAtStringKey(name,key);}
static int MainEX12_GetNodeNameCount(struct string *name,int id) {MainEX12_value(name,id);return MainEXFile_GetNodeNameCount(name);}
static int MainEX12_GetEXNameCount(struct string *name,int id) {MainEX12_value(name,id);return MainEXFile_GetEXNameCount(name);}
static bool MainEX12_GetNodeName(struct string *name,int i,struct string **out,int id) {MainEX12_value(name,id);return MainEXFile_GetNodeName(name,i,out);}
static bool MainEX12_GetEXName(struct string *name,int i,struct string **out,int id) {MainEX12_value(name,id);return MainEXFile_GetEXName(name,i,out);}
static bool MainEX12_NameList(struct string *name,struct page **list,int id,bool leaf)
{
	struct ex_value *v=MainEX12_value(name,id);
	if(!list || !*list || (*list)->a_type!=AIN_ARRAY || !(*list)->element_type ||
		(*list)->element_type->data!=AIN_STRING || (*list)->array.rank!=1)
		VM_ERROR("EX name list requires a typed string array");
	int count=leaf?MainEXFile_GetEXNameCount(name):MainEXFile_GetNodeNameCount(name);
	*list=generic_array_resize(*list,count,false);
	for(int i=0;i<count;i++) {
		struct string *s=NULL;
		bool found=leaf?MainEXFile_GetEXName(name,i,&s):MainEXFile_GetNodeName(name,i,&s);
		if(!found) VM_ERROR("EX child count changed during enumeration");
		variable_set(*list,i,AIN_STRING,vm_int(heap_alloc_string(s)));
	}
	return v && v->type==EX_TREE && !v->tree->is_leaf;
}
static bool MainEX12_GetNodeNameList(struct string *name,struct page **list,int id) {return MainEX12_NameList(name,list,id,false);}
static bool MainEX12_GetEXNameList(struct string *name,struct page **list,int id) {return MainEX12_NameList(name,list,id,true);}
#define EX12_REPLACE_LIST(X) \
 X(Row) X(Col) X(Type) X(AType) X(A2Type) X(Exists) X(AExists) X(A2Exists) \
 X(Int) X(Float) X(String) X(AInt) X(AFloat) X(AString) X(A2Int) X(A2Float) X(A2String) \
 X(GetRowAtIntKey) X(GetRowAtStringKey) X(GetNodeNameCount) X(GetEXNameCount) X(GetNodeName) X(GetEXName)
