/* AIN12 queries return values and accept a default plus reader ID.
 * Reader 0 is the main EX file. Additional readers are not implemented. */
static struct ex_value *MainEX12_value(struct string *name,int id)
{
	if(id!=0) VM_ERROR("Additional EX reader %d is not implemented",id);
	return ex_get(ex,name->text);
}
static struct ex_value *MainEX12_at(struct string *name,int index,int id)
{
	struct ex_value *v=MainEX12_value(name,id);
	return v && v->type==EX_LIST && index>=0 ? ex_list_get(v->list,index):NULL;
}
static struct ex_value *MainEX12_at2(struct string *name,int row,int col,int id)
{
	struct ex_value *v=MainEX12_value(name,id);
	return v && v->type==EX_TABLE && row>=0 && col>=0 ? ex_table_get(v->t,row,col):NULL;
}
#define EX12_VALUES(PREFIX,PARAMS,GET) \
static int PREFIX##Int PARAMS(int dflt,int id) {struct ex_value *v=GET;return v && v->type==EX_INT?v->i:dflt;} \
static float PREFIX##Float PARAMS(float dflt,int id) {struct ex_value *v=GET;return v && v->type==EX_FLOAT?v->f:dflt;} \
static struct string *PREFIX##String PARAMS(struct string *dflt,int id) {struct ex_value *v=GET;return string_ref(v && v->type==EX_STRING?v->s:dflt);}
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
