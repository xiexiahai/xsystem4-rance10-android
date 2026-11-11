/* AIN 11+ VM-facing libraries. Included in vm.c to share the operand stack.
 * Unknown overloads fail explicitly; they are never silently stubbed. */
static int ain12_array_width(struct page *p)
{
	return p->array.rank==1 && p->element_type && p->element_type->data == AIN_IFACE ? 2 : 1;
}

static void ain12_resize(int slot, int count, bool construct)
{
	struct page *p = heap_get_page(slot);
	int old = p->nr_vars;
	heap_set_page(slot, generic_array_resize(p, count, false));
	p = heap_get_page(slot);
	if (construct && p->array.rank==1 && p->element_type->data == AIN_STRUCT) {
		int sno = p->element_type->struc;
		for (int i = old; i < count; i++) {
			union vm_value v;
			create_struct(sno, &v);
			heap_get_page(slot)->values[i] = v;
		}
	}
}

/* AIN12 describes axes either as rank>1 or nested ARRAY descriptors. Validate
 * the entire requested shape before replacing pages, then consume one axis at
 * each physical child page. Omitted nested axes deliberately stay empty. */
static void ain12_validate_alloc_shape(int rank, const struct ain_type *element, int axes)
{
	if (rank<1 || rank>4 || !element || axes<rank)
		VM_ERROR("Array.Alloc dimensions do not match the array descriptor");
	if (axes>rank) {
		if (element->data!=AIN_ARRAY || !element->array_type)
			VM_ERROR("Array.Alloc has more dimensions than its descriptor");
		ain12_validate_alloc_shape(element->rank,element->array_type,axes-rank);
	}
}

static void ain12_alloc_shape(int slot, const int *dimensions, int axes)
{
	int rank=heap_get_page(slot)->array.rank;
	ain12_resize(slot,0,false);ain12_resize(slot,dimensions[0],rank==1);
	struct page *p=heap_get_page(slot);
	if (rank>1 || (axes>1 && p->element_type->data==AIN_ARRAY))
		for (int i=0;i<dimensions[0];i++)
			ain12_alloc_shape(heap_get_page(slot)->values[i].i,dimensions+1,axes-1);
}

static bool ain12_array_erase(int slot,int index)
{
	struct page *p=heap_get_page(slot);int width=ain12_array_width(p),count=p->nr_vars/width;
	if(index<0 || index>=count) return false;
	union vm_value old[2];enum ain_data_type types[2];
	for(int i=0;i<width;i++) {old[i]=p->values[index*width+i];types[i]=variable_type(p,index*width+i,NULL,NULL);}
	memmove(p->values+index*width,p->values+(index+1)*width,sizeof(*p->values)*(count-index-1)*width);
	for(int i=0;i<width;i++) p->values[(count-1)*width+i]=variable_initval(types[i]);
	ain12_resize(slot,count-1,false);
	for(int i=0;i<width;i++) variable_fini(old[i],types[i],true);
	return true;
}

static int ain12_array_callback(int slot, const int *indices, int arguments, int object, int function, int parent)
{
	if (function<0 || function>=ain->nr_functions) VM_ERROR("Invalid generic callback");
	struct ain_function *f=&ain->functions[function];
	struct page *p=heap_get_page(slot);
	int before=stack_ptr;
	stack_push(object);
	int formal=0;
	for(int arg=0;arg<arguments;arg++) {
		if(formal>=f->nr_args) VM_ERROR("Missing generic callback parameter");
		p=heap_get_page(slot);
		int index=indices[arg];
		enum ain_data_type type=f->vars[formal].type.data;
	switch (type) {
	case AIN_REF_INT: case AIN_REF_FLOAT: case AIN_REF_BOOL: case AIN_REF_ENUM:
		stack_push(slot);stack_push(index);formal++;break;
	case AIN_REF_STRUCT: case AIN_REF_STRING: case AIN_REF_ARRAY:
		stack_push(p->values[index]);break;
	case AIN_IFACE:
		stack_push(p->values[index*2]);stack_push(p->values[index*2+1]);formal++;break;
	default:
		stack_push(vm_copy(p->values[index],type));break;
	}
		formal++;
	}
	if (stack_ptr-before!=f->nr_args+1) VM_ERROR("Unsupported generic callback signature");
	size_t saved_ip=instr_ptr;
	method_call(function,VM_RETURN);
	page_set_local_parent(local_page(),parent);
	vm_execute();instr_ptr=saved_ip;
	if (stack_ptr!=before+1) VM_ERROR("Generic callback stack imbalance");
	return stack_pop().i;
}

static int ain12_array_predicate(int slot, int index, int object, int function, int parent)
{
	return ain12_array_callback(slot,&index,1,object,function,parent);
}

#include "ain12_array_extrema.inc"
#include "ain12_array_order.inc"
#include "ain12_array_concat.inc"
#include "ain12_array_firstlast.inc"
#include "ain12_array_where.inc"
#include "ain12_array_erase_all.inc"
#include "ain12_array_erase_set.inc"

static bool ain12_hll_call(int libno, int fno, int type_param)
{
	const char *lib = ain->libraries[libno].name;
	struct ain_hll_function *f = &ain->libraries[libno].functions[fno];
	if (getenv("XSYSTEM4_TRACE_HLL")) {
		/* Native AIN12 library calls never reach ffi.c, so log them here.
		 * Bounded: each distinct "Library.Function/n" is printed a few times. */
		static char seen[256][72];
		static int hits[256];
		static int seen_n;
		char key[72];
		snprintf(key, sizeof(key), "%s.%s/%d", lib, f->name, f->nr_arguments);
		int slot = -1;
		for (int i = 0; i < seen_n; i++)
			if (!strcmp(seen[i], key)) { slot = i; break; }
		if (slot < 0 && seen_n < 256) { slot = seen_n++; strncpy(seen[slot], key, sizeof(seen[0]) - 1); }
		if (slot >= 0 && hits[slot] < 3) {
			hits[slot]++;
			sys_message("[ain12] %s\n", key);
		}
	}
	if (!strcmp(lib,"Math") && !strcmp(f->name,"Abs")) {
		/* AIN12 gives both overloads the same name. The legacy static Abs
		 * export is C abs(int); passing floats through that FFI signature
		 * leaves fractional easing inputs invalid. Dispatch by signature. */
		if (f->nr_arguments==1 && f->arguments[0].type.data==AIN_FLOAT &&
				f->return_type.data==AIN_FLOAT) {
			stack_push(vm_float(fabsf(stack_pop().f)));
			return true;
		}
		if (f->nr_arguments!=1 || f->arguments[0].type.data!=AIN_INT ||
				f->return_type.data!=AIN_INT)
			VM_ERROR("Unsupported Math.Abs signature");
		return false; // The legacy integer export has the matching C ABI.
	}
	if(!strcmp(lib,"String") && !strcmp(f->name,"Split") && f->nr_arguments==3) {
		bool contain=stack_pop().i;int separators=stack_pop().i,self=stack_pop().i;
		struct string *s=heap_get_string(self),*sep=heap_get_string(separators);
		struct ain_type element=AIN_STRING_TYPE,array={.data=AIN_ARRAY,.rank=1,.array_type=&element};
		int result=variable_init(&array).i,count=0,start=0;
		for(int i=0;i<s->size;) {
			int width=SJIS_2BYTE(s->text[i]) && i+1<s->size?2:1;bool delimiter=false;
			for(int j=0;j<sep->size;) {
				int size=SJIS_2BYTE(sep->text[j]) && j+1<sep->size?2:1;
				if(width==size && !memcmp(s->text+i,sep->text+j,width)) delimiter=true;
				j+=size;
			}
			if(delimiter) {
				// Included separators are tokens themselves; omit empty spans
				// around them. The exclude-separators mode retains its old policy.
				if (!contain || i > start) {
					ain12_resize(result,count+1,false);
					variable_set(heap_get_page(result),count++,AIN_STRING,vm_int(heap_alloc_string(make_string(s->text+start,i-start))));
				}
				if(contain) {
					ain12_resize(result,count+1,false);
					variable_set(heap_get_page(result),count++,AIN_STRING,vm_int(heap_alloc_string(make_string(s->text+i,width))));
				}
				start=i+width;
			}
			i+=width;
		}
		if (!contain || start < s->size) {
			ain12_resize(result,count+1,false);
			variable_set(heap_get_page(result),count,AIN_STRING,vm_int(heap_alloc_string(make_string(s->text+start,s->size-start))));
		}
		heap_unref(separators);stack_push(result);return true;
	}
	if(!strcmp(lib,"Math") && (!strcmp(f->name,"Clamp") || !strcmp(f->name,"Min") || !strcmp(f->name,"Max"))) {
		int n=f->nr_arguments;
		if(n<2 || n>4 || (!strcmp(f->name,"Clamp") && n!=3)) VM_ERROR("Invalid Math overload");
		union vm_value values[4];for(int i=n-1;i>=0;i--) values[i]=stack_pop();
		if(f->return_type.data==AIN_FLOAT) {
			float result=values[0].f;
			if(!strcmp(f->name,"Clamp")) result=fmaxf(values[1].f,fminf(result,values[2].f));
			else for(int i=1;i<n;i++) result=!strcmp(f->name,"Min")?fminf(result,values[i].f):fmaxf(result,values[i].f);
			stack_push(vm_float(result));
		} else {
			int result=values[0].i;
			if(!strcmp(f->name,"Clamp")) result=max(values[1].i,min(result,values[2].i));
			else for(int i=1;i<n;i++) result=!strcmp(f->name,"Min")?min(result,values[i].i):max(result,values[i].i);
			stack_push(result);
		}
		return true;
	}
	if (!strcmp(lib,"String") && !strcmp(f->name,"Erase") && f->nr_arguments==3) {
		int length=stack_pop().i, index=stack_pop().i, slot=stack_pop().i;
		struct string *s=heap_get_string(slot);
		int count=sjis_count_char(s->text);
		if(index<0) index=0;
		if(length>0 && index<count) {
			if(length>count-index) length=count-index;
			struct string *result=string_copy(s,0,index), *tail=string_copy(s,index+length,count);
			string_append(&result,tail);free_string(tail);free_string(s);heap[slot].s=result;
		}
		return true;
	}
	if (!strcmp(lib,"String") && !strcmp(f->name,"Replace") && f->nr_arguments==3 &&
		f->return_type.data==AIN_STRING && f->arguments[0].type.data==AIN_REF_STRING &&
		f->arguments[1].type.data==AIN_STRING && f->arguments[2].type.data==AIN_STRING) {
		int replacement=stack_pop().i,key=stack_pop().i,self=stack_pop().i;
		struct string *result=ain12_string_replace_all(heap_get_string(self),
			heap_get_string(key),heap_get_string(replacement));
		if (!result) VM_ERROR("String.Replace exceeds the bounded byte/character contract");
		heap_unref(key);heap_unref(replacement);stack_push_string(result);return true;
	}
	if (!strcmp(lib,"String") && !strcmp(f->name,"ReplaceRegex") && f->nr_arguments==3 &&
		f->arguments && f->return_type.data==AIN_STRING && !f->return_type.rank && !f->return_type.array_type &&
		f->arguments[0].type.data==AIN_REF_STRING && !f->arguments[0].type.rank && !f->arguments[0].type.array_type &&
		f->arguments[1].type.data==AIN_STRING && !f->arguments[1].type.rank && !f->arguments[1].type.array_type &&
		f->arguments[2].type.data==AIN_STRING && !f->arguments[2].type.rank && !f->arguments[2].type.array_type) {
		int replacement=stack_pop().i,pattern=stack_pop().i,self=stack_pop().i;
		enum ain12_regex_error error;
		struct string *result=ain12_string_regex_replace(heap_get_string(self),
			heap_get_string(pattern),heap_get_string(replacement),&error);
		if (!result) VM_ERROR("String.ReplaceRegex: unsupported grammar/replacement or bounded limit (%d)",error);
		heap_unref(pattern);heap_unref(replacement);stack_push_string(result);return true;
	}
	// String.PushBack(ref string self, int Chara) and String.PopBack(ref string
	// self) mutate the borrowed receiver slot. The existing helpers apply
	// copy-on-write through string_realloc/string_pop_back, so heap[slot].s may
	// be replaced while the receiver slot's own heap reference is left alone.
	if (!strcmp(lib,"String") && !strcmp(f->name,"PushBack") && f->nr_arguments==2 &&
		f->arguments[0].type.data==AIN_REF_STRING && f->arguments[1].type.data==AIN_INT) {
		int character=stack_pop().i, slot=stack_pop().i;
		string_push_back(&heap[slot].s, character);
		return true;
	}
	if (!strcmp(lib,"String") && !strcmp(f->name,"PopBack") && f->nr_arguments==1 &&
		f->arguments[0].type.data==AIN_REF_STRING) {
		int slot=stack_pop().i;
		string_pop_back(&heap[slot].s);
		return true;
	}
	if (!strcmp(lib,"String") && f->nr_arguments==2 &&
		(!strcmp(f->name,"StartsWith") || !strcmp(f->name,"EndsWith") || !strcmp(f->name,"Contains"))) {
		int needle=stack_pop().i, self=stack_pop().i;
		struct string *s=heap_get_string(self), *part=heap_get_string(needle);
		bool result;
		if (!strcmp(f->name,"Contains")) result=string_find(s,part)>=0;
		else result=part->size<=s->size && !memcmp(s->text+
			(!strcmp(f->name,"EndsWith") ? s->size-part->size : 0),part->text,part->size);
		heap_unref(needle);stack_push(result);return true;
	}
	if (!strcmp(lib,"String") && (!strcmp(f->name,"Find") || !strcmp(f->name,"FindLast")) && f->nr_arguments==2) {
		int needle=stack_pop().i, self=stack_pop().i;
		struct string *s=heap_get_string(self), *part=heap_get_string(needle);
		int result=-1;
		if (!strcmp(f->name,"Find")) result=string_find(s,part);
		else for(int i=0,c=0;i<s->size;c++) {
			if(part->size<=s->size-i && !memcmp(s->text+i,part->text,part->size)) result=c;
			i+=SJIS_2BYTE(s->text[i]) && i+1<s->size?2:1;
		}
		heap_unref(needle);stack_push(result);return true;
	}
	if (!strcmp(lib,"String") && !strcmp(f->name,"GetPart") && (f->nr_arguments==2 || f->nr_arguments==3)) {
		int length=f->nr_arguments==3?stack_pop().i:INT32_MAX;
		int index=stack_pop().i, slot=stack_pop().i;
		struct string *s=heap_get_string(slot);
		if(length==INT32_MAX) length=sjis_count_char(s->text);
		stack_push_string(string_copy(s,index,length));return true;
	}
	if ((!strcmp(lib,"Int") || !strcmp(lib,"Float")) && !strcmp(f->name,"String")) {
		int precision=0;
		if (!strcmp(lib,"Float")) precision=stack_pop().i;
		union vm_value value=*stack_pop_var();
		stack_push_string(!strcmp(lib,"Float") ? float_to_string(value.f,precision) : integer_to_string(value.i));
		return true;
	}
	if (!strcmp(lib,"String") && f->nr_arguments==1) {
		struct string *s=heap_get_string(stack_peek(0).i);
		union vm_value result;
		if (!strcmp(f->name,"Empty")) result=vm_int(s->size==0);
		else if (!strcmp(f->name,"Length")) result=vm_int(sjis_count_char(s->text));
		else if (!strcmp(f->name,"LengthByte")) result=vm_int(s->size);
		else if (!strcmp(f->name,"Int")) result=vm_int(string_to_integer(s));
		else if (!strcmp(f->name,"Float")) result=vm_float(strtof(s->text,NULL));
		else return false;
		stack_pop();stack_push(result);return true;
	}
	if (!strcmp(lib, "system")) {
		if (!strcmp(f->name,"ExistSaveFile")) {system_call(SYS_EXISTS_SAVE_FILE);return true;}
		for (int i = 0; i < NR_SYSCALLS; i++) {
			const char *name = syscalls[i].name;
			if (name && !strncmp(name, "system.", 7) && !strcmp(name + 7, f->name)) {
				system_call(i);
				return true;
			}
		}
		return false;
	}
	if (strcmp(lib, "Array"))
	{
		if (!strcmp(lib,"Delegate") && f->nr_arguments==2) {
			const char *name=f->name;
			if (strcmp(name,"Set") && strcmp(name,"Add") && strcmp(name,"Exist") && strcmp(name,"Erase")) return false;
			int function=stack_pop().i, object=stack_pop().i, slot=stack_pop().i;
			if(function<0 || function>=ain->nr_functions) VM_ERROR("Invalid Delegate function");
			struct page *dg=heap_get_delegate_page(slot);
			int environment=active_lambda_environment(function);
			if (!strcmp(name,"Set")) {
				struct page *replacement=delegate_new_from_method(object,function);
				if(environment>=0 && replacement->nr_vars) delegate_set_environment(replacement,0,environment);
				if(dg) {delete_page_vars(dg);free_page(dg);}heap_set_page(slot,replacement);
			}
			else if (!strcmp(name,"Add")) {
				bool exists=delegate_contains(dg,object,function);
				dg=delegate_append(dg,object,function);heap_set_page(slot,dg);
				if(!exists && environment>=0) delegate_set_environment(dg,dg->nr_vars/3-1,environment);
			}
			else if (!strcmp(name,"Exist")) stack_push(delegate_contains(dg,object,function));
			else delegate_erase(dg,object,function);
			return true;
		}
		if (!strcmp(lib, "Delegate") && f->nr_arguments == 1) {
			int slot = stack_pop().i;
			struct page *dg = heap_get_delegate_page(slot);
			if (!strcmp(f->name, "Numof")) stack_push(delegate_numof(dg));
			else if (!strcmp(f->name, "Empty")) stack_push(delegate_numof(dg)==0);
			else if (!strcmp(f->name, "Clear")) heap_set_page(slot,delegate_clear(dg));
			else { stack_push(slot); return false; }
			return true;
		}
		return false;
	}
	if (ain12_array_erase_set(f,type_param))
		return true;
	if (ain12_array_extrema(f))
		return true;
	if (ain12_array_firstlast(f))
		return true;
	if (!strcmp(f->name,"Where"))
		return ain12_array_where(f);
	if (!strcmp(f->name,"EraseAll"))
		return ain12_array_erase_all(f);
	if (ain12_array_order(f, type_param))
		return true;
	if (!strcmp(f->name, "Concat"))
		return ain12_array_concat(f);
	// Predicate overloads contain a receiver/function pair and capture the
	// calling local frame for X_GETENV. Execute synchronously in the VM.
	if (f->nr_arguments==2 && f->arguments[1].type.data==AIN_HLL_FUNC) {
		const char *name=f->name;
		if(!strcmp(name,"QuickSort") || !strcmp(name,"Sort")) {
			if (f->arguments[0].type.data!=AIN_REF_ARRAY || f->return_type.data!=AIN_REF_ARRAY) return false;
			int function=stack_pop().i, object=stack_pop().i, slot=stack_pop().i;
			if(function<0 || function>=ain->nr_functions) VM_ERROR("Invalid sort callback");
			struct page *p=heap_get_page(slot);
			if (!p || p->type!=ARRAY_PAGE || p->a_type!=AIN_ARRAY || !p->element_type || p->nr_vars<0)
				VM_ERROR("Array sort needs a generic array");
			int width=ain12_array_width(p), count=p->nr_vars/width;
			if(p->array.rank!=1 || p->nr_vars%width || ain->functions[function].return_type.data!=AIN_BOOL)
				VM_ERROR("Unsupported generic sort comparator");
			struct ain_type element=*p->element_type;
			int parent=local_page_slot();
			int *order=xmalloc(sizeof(int)*(count?count:1)), *scratch=xmalloc(sizeof(int)*(count?count:1));
			for(int i=0;i<count;i++) order[i]=i;
			for(int run=1;run<count;run*=2) {
				for(int start=0;start<count;start+=run*2) {
					int mid=min(start+run,count), end=min(start+run*2,count), left=start,right=mid;
					for(int out=start;out<end;out++) {
						int pair[2]={right<end?order[right]:0,left<mid?order[left]:0};
						bool take_right=right<end && (left==mid || ain12_array_callback(slot,pair,2,object,function,parent));
						if (!page_index_valid(slot)) VM_ERROR("Array sort callback released its receiver");
						struct page *after=heap_get_page(slot);
						if (!after || after->type!=ARRAY_PAGE || after->a_type!=AIN_ARRAY ||
							after->array.rank!=1 || ain12_array_width(after)!=width ||
							after->nr_vars!=count*width || !after->element_type ||
							!ain12_firstlast_type_equal(&element,after->element_type,0))
							VM_ERROR("Array sort callback changed the receiver shape");
						scratch[out]=take_right?order[right++]:order[left++];
					}
				}
				int *swap=order;order=scratch;scratch=swap;
			}
			p=heap_get_page(slot);
			if(p->nr_vars!=count*width) VM_ERROR("Array modified during sort comparator");
			union vm_value *values=xmalloc(sizeof(*values)*(count?count:1)*width);
			for(int i=0;i<count;i++)
				for(int k=0;k<width;k++) values[i*width+k]=p->values[order[i]*width+k];
			memcpy(p->values,values,sizeof(*values)*count*width);
			free(values);free(order);free(scratch);heap_ref(slot);stack_push(slot);return true;
		}
		bool binary=!strcmp(name,"BinarySearch"), lower=!strcmp(name,"LowerBound"), upper=!strcmp(name,"UpperBound");
		bool find=!strcmp(name,"Find"), any=!strcmp(name,"Any"), exist=!strcmp(name,"IsExist");
		bool erase=!strcmp(name,"Erase");
		bool first=!strcmp(name,"First"),last=!strcmp(name,"Last");
		bool all=!strcmp(name,"All"), count_true=!strcmp(name,"Numof"), find_last=!strcmp(name,"FindLast");
		if ((all || count_true || find_last) && f->arguments[0].type.data != AIN_REF_ARRAY) return false;
		if (!binary && !lower && !upper && !find && !any && !exist && !erase && !first && !last &&
			!all && !count_true && !find_last) return false;
		int function=stack_pop().i, object=stack_pop().i, slot=stack_pop().i;
		if (all || count_true || find_last) {
			struct page *receiver=heap_get_page(slot);
			if (!receiver || receiver->type!=ARRAY_PAGE || receiver->a_type!=AIN_ARRAY ||
				!receiver->element_type || receiver->array.rank!=1 || receiver->nr_vars%ain12_array_width(receiver))
				VM_ERROR("Array.%s needs a rank-one generic array",name);
		}
		int parent=local_page_slot(), count=heap_get_page(slot)->nr_vars/ain12_array_width(heap_get_page(slot));
		int found=-1;
		// All/Numof/FindLast reuse the predicate scan and the caller's local
		// frame. The callback may mutate the receiver through HLL calls, so the
		// shape is revalidated before every element access.
		if (all || count_true || find_last) {
			struct page *src=heap_get_page(slot);
			if (!src->element_type || src->array.rank != 1)
				VM_ERROR("Array.%s only supports rank-1 typed arrays", name);
			int width=ain12_array_width(src), total=src->nr_vars/width;
			struct ain_type element=*src->element_type;
			bool all_true=true; int matches=0;
			for(int step=0;step<total;step++) {
				int i=find_last?total-1-step:step;
				int result=ain12_array_predicate(slot,i,object,function,parent);
				if (!page_index_valid(slot)) VM_ERROR("Array.%s callback released its receiver",name);
				struct page *after=heap_get_page(slot);
				if(!after || after->type!=ARRAY_PAGE || after->a_type!=AIN_ARRAY ||
					after->array.rank != 1 || ain12_array_width(after) != width ||
					after->nr_vars != total*width || !after->element_type ||
					after->element_type->data != element.data ||
					after->element_type->struc != element.struc)
					VM_ERROR("Array.%s callback changed the array shape", name);
				if (find_last) {
					if (result) { found=i; break; }
				} else if (all) {
					if (!result) { all_true=false; break; }
				} else if (result) {
					matches++;
				}
			}
			if (find_last) stack_push(found);
			else if (all) stack_push(all_true);
			else stack_push(matches);
			return true;
		}
		for(int step=0;step<count;step++) {
			int i=last?count-1-step:step;
			int result=ain12_array_predicate(slot,i,object,function,parent);
			if ((binary && result==0) || (lower && result>=0) || (upper && result>0) ||
				((find || any || exist || first || last || erase) && result)) {found=i;break;}
		}
		if(erase) {stack_push(ain12_array_erase(slot,found));return true;}
		if (first || last) {
			struct page *src=heap_get_page(slot);
			enum ain_data_type type=src->element_type->data;
			if(type==AIN_IFACE) {
				int value=found>=0?src->values[found*2].i:-1;
				heap_ref(value);stack_push(value);stack_push(found>=0?src->values[found*2+1].i:0);return true;
			}
			if(ain12_array_width(src)!=1 || (type!=AIN_STRUCT && type!=AIN_REF_STRUCT && type!=AIN_STRING && type!=AIN_ARRAY))
				VM_ERROR("First/Last predicate result type %d is not implemented",type);
			int value=found>=0?src->values[found].i:-1;heap_ref(value);stack_push(value);return true;
		}
		if ((lower || upper) && found<0) found=count;
		stack_push((any || exist) ? found>=0 : found);return true;
	}
	for (int i = 0; i < f->nr_arguments; i++)
		if (f->arguments[i].type.data == AIN_HLL_FUNC)
			return false;
	int n = f->nr_arguments;
	int physical=n;
	if(type_param==AIN_IFACE)
		for(int i=0;i<n;i++) if(f->arguments[i].type.data==AIN_HLL_PARAM || f->arguments[i].type.data==AIN_REF_HLL_PARAM) physical++;
	if (n < 1 || n > 5 || stack_ptr < physical)
		VM_ERROR("Invalid Array.%s argument count", f->name);
	int slot = stack_peek(physical-1).i;
	struct page *p = heap_get_page(slot);
	if (!p || p->type != ARRAY_PAGE || p->a_type != AIN_ARRAY)
		VM_ERROR("Array.%s needs a typed generic array", f->name);
	int width = ain12_array_width(p);
	int count = p->nr_vars / width;
	const char *name = f->name;
	// Reverse swaps rank-1 logical elements in place. Interface elements occupy
	// two adjacent slots, so both slots move together; no element is copied or
	// reference-counted here.
	if (!strcmp(name, "Reverse") && n == 1 && f->arguments[0].type.data == AIN_REF_ARRAY) {
		if (p->array.rank != 1)
			VM_ERROR("Array.Reverse only supports rank-1 arrays");
		for (int left=0, right=count-1; left<right; left++, right--)
			for (int k=0; k<width; k++) {
				union vm_value tmp=p->values[left*width+k];
				p->values[left*width+k]=p->values[right*width+k];
				p->values[right*width+k]=tmp;
			}
		stack_ptr -= n;
	} else if (!strcmp(name, "Alloc")) {
		int dimensions[4]={stack_peek(3).i,stack_peek(2).i,stack_peek(1).i,stack_peek(0).i};
		if (n != 5 || p->array.rank<1 || p->array.rank>4)
			VM_ERROR("Unsupported Array.Alloc shape");
		int axes=0;
		for(int i=0;i<4;i++) {
			if (dimensions[i]==-1) continue;
			if (i!=axes || dimensions[i]<0 || dimensions[i]>10000000)
				VM_ERROR("Invalid Array.Alloc dimension %d",i);
			axes++;
		}
		ain12_validate_alloc_shape(p->array.rank,p->element_type,axes);
		stack_ptr -= n;
		ain12_alloc_shape(slot,dimensions,axes);
	} else if (!strcmp(name, "Free")) {
		stack_ptr -= n;
		ain12_resize(slot, 0, false);
	} else if (!strcmp(name, "Numof") || !strcmp(name, "Empty") || !strcmp(name, "Any")) {
		stack_ptr -= n;
		stack_push(!strcmp(name, "Numof") ? count :
			(!strcmp(name, "Empty") ? count == 0 : count != 0));
	} else if (!strcmp(name, "Realloc") && n == 2) {
		int size = stack_pop().i;
		stack_pop();
		ain12_resize(slot, size, true);
	} else if (!strcmp(name,"PushBack") && n==2 && width==2 && type_param==AIN_IFACE) {
		int vtable=stack_pop().i, object=stack_pop().i;stack_pop();
		ain12_resize(slot,count+1,false);p=heap_get_page(slot);
		heap_ref(object);p->values[count*2]=vm_int(object);p->values[count*2+1]=vm_int(vtable);
	} else if (!strcmp(name, "PushBack") && n == 2 && width == 1) {
		union vm_value value = stack_pop();
		stack_pop();
		ain12_resize(slot, count+1, false);
		p = heap_get_page(slot);
		switch (p->element_type->data) {
		case AIN_REF_TYPE: heap_ref(value.i); break;
		default: break;
		}
		variable_set(p, count, p->element_type->data, value);
	} else if (!strcmp(name, "PopBack")) {
		stack_ptr -= n;
		if (count) ain12_resize(slot, count-1, false);
	} else if (!strcmp(name,"Erase") && n==2 && f->arguments[1].type.data==AIN_INT) {
		int index=stack_pop().i;stack_pop();
		stack_push(ain12_array_erase(slot,index));
	} else if (!strcmp(name,"Unique") && n==1 && width==1) {
		enum ain_data_type type=p->element_type->data;
		if(type!=AIN_STRING && type!=AIN_INT && type!=AIN_BOOL && type!=AIN_ENUM)
			VM_ERROR("Array.Unique element type %d is not implemented",type);
		stack_pop();
		int kept=0;
		for(int i=0;i<count;i++) {
			bool duplicate=false;
			for(int j=0;j<kept;j++) {
				if(type==AIN_STRING ? !strcmp(heap_get_string(p->values[j].i)->text,heap_get_string(p->values[i].i)->text) :
					p->values[j].i==p->values[i].i) {duplicate=true;break;}
			}
			if(duplicate) {
				variable_fini(p->values[i],type,true);p->values[i]=vm_int(-1);
			} else {
				if(kept!=i) {p->values[kept]=p->values[i];p->values[i]=vm_int(-1);}
				kept++;
			}
		}
		ain12_resize(slot,kept,false);
	} else if(!strcmp(name,"Shuffle")) {
		if (n != 2) {
			if (getenv("XSYSTEM4_TRACE_HLL"))
				sys_message("[shuffle] unsupported arg count n=%d slot=%d count=%d\n", n, slot, count);
			return false;
		}
		uint32_t seed=stack_pop().i;stack_pop();
		/* Rance 10 passes -1 to mean "use a fresh random seed". Feeding that
		 * literal into mt19937 makes every shuffle reproducible, so the battle
		 * shuffle button produced the same order each time. */
		if (seed == 0xFFFFFFFFu) {
			static struct mt19937 sysrand;
			static bool sysrand_init;
			if (!sysrand_init) {
				mt19937_init(&sysrand, (uint32_t)time(NULL));
				sysrand_init = true;
			}
			seed = mt19937_genrand(&sysrand);
		}
		static unsigned shuffle_calls;
		if (getenv("XSYSTEM4_TRACE_HLL") && shuffle_calls < 200) {
			shuffle_calls++;
			char before[96]; int used = 0; before[0] = 0;
			for (int i = 0; i < count && i < 8 && used < 80; i++) {
				used += snprintf(before + used, sizeof(before) - used, "%s%d", i ? "," : "", p->values[i*width].i);
			}
			sys_message("[shuffle] call=%u slot=%d count=%d width=%d seed=%u before=%s\n", shuffle_calls, slot, count, width, seed, before);
		}
		struct mt19937 random;mt19937_init(&random,seed);
		for(int i=count-1;i>0;i--) {
			uint32_t bound=i+1,threshold=(uint32_t)(-bound)%bound,value;
			do {value=mt19937_genrand(&random);} while(value<threshold);
			int other=value%bound;
			for(int k=0;k<width;k++) {
				union vm_value tmp=p->values[i*width+k];p->values[i*width+k]=p->values[other*width+k];p->values[other*width+k]=tmp;
			}
		}
		if (getenv("XSYSTEM4_TRACE_HLL") && shuffle_calls) {
			char after[96]; int used = 0; after[0] = 0;
			for (int i = 0; i < count && i < 8 && used < 80; i++) {
				used += snprintf(after + used, sizeof(after) - used, "%s%d", i ? "," : "", p->values[i*width].i);
			}
			sys_message("[shuffle] after=%s\n", after);
		}
	} else if ((!strcmp(name, "Find") || !strcmp(name, "FindLast") || !strcmp(name, "IsExist")) && (n==2 || n==4) && width == 1) {
		union vm_value value = stack_pop();int begin=0,end=count;
		if(n==4) {end=min(count,stack_pop().i);begin=max(0,stack_pop().i);}
		stack_pop();
		int found = -1;
		for (int k = 0; k < end-begin; k++) {
			int i = !strcmp(name, "FindLast") ? end-1-k : begin+k;
			bool equal = type_param == AIN_STRING ?
				!strcmp(heap_get_string(p->values[i].i)->text, heap_get_string(value.i)->text) :
				p->values[i].i == value.i;
			if (equal) { found=i; break; }
		}
		switch (type_param) {
		case AIN_REF_TYPE: break;
		default: variable_fini(value, type_param, true); break;
		}
		stack_push(!strcmp(name, "IsExist") ? found >= 0 : found);
	} else if (!strcmp(name, "EmplaceBack") && n == 1 && width == 1) {
		stack_pop();
		ain12_resize(slot, count+1, true);
		p = heap_get_page(slot);
		enum ain_data_type e = p->element_type->data;
		if (e == AIN_INT || e == AIN_FLOAT || e == AIN_BOOL || e == AIN_ENUM) {
			heap_ref(slot);
			stack_push(slot); stack_push(count);
		} else {
			heap_ref(p->values[count].i);
			stack_push(p->values[count]);
		}
	} else if ((!strcmp(name,"LowerBound") || !strcmp(name,"UpperBound")) && n==2 && width==1 && type_param==AIN_INT) {
		int value=stack_pop().i;stack_pop();
		int i=0;bool upper=!strcmp(name,"UpperBound");
		while(i<count && (p->values[i].i<value || (upper && p->values[i].i==value))) i++;
		stack_push(i);
	} else if (!strcmp(name,"Insert") && n==3 && width==1) {
		union vm_value value=stack_pop();int index=stack_pop().i;stack_pop();
		if(index<0 || index>count) VM_ERROR("Array.Insert index out of bounds");
		ain12_resize(slot,count+1,false);p=heap_get_page(slot);
		variable_fini(p->values[count],p->element_type->data,true);
		memmove(p->values+index+1,p->values+index,(count-index)*sizeof(*p->values));
		if (type_param==AIN_REF_STRUCT || type_param==AIN_REF_STRING || type_param==AIN_REF_ARRAY) heap_ref(value.i);
		p->values[index]=value;
	} else if (!strcmp(name,"Fill") && (n==2 || n==4) && width==1) {
		union vm_value value=stack_pop();
		int length=count,index=0;
		if(n==4) {length=stack_pop().i;index=stack_pop().i;}
		stack_pop();
		if(index<0 || length<0 || index>count || length>count-index)
			VM_ERROR("Array.Fill range out of bounds");
		for(int i=0;i<length;i++)
			variable_set(p,index+i,p->element_type->data,vm_copy(value,p->element_type->data));
		switch(type_param) {
		case AIN_REF_TYPE: break;
		default: variable_fini(value,type_param,true); break;
		}
		stack_push(length);
	} else if ((!strcmp(name, "At") || !strcmp(name, "First") || !strcmp(name, "Last")) && n <= 2) {
		int index = n == 2 ? stack_pop().i : (!strcmp(name, "Last") ? count-1 : 0);
		stack_pop();
		enum ain_data_type e = p->element_type->data;
		if (index < 0 || index >= count) {
			if(width==2 || e==AIN_INT || e==AIN_FLOAT || e==AIN_BOOL || e==AIN_ENUM) {
				stack_push(-1);stack_push(0);return true;
			}
			// Object-reference At results are optional. The AIN checks -1 and
			// supplies its own default for omitted user-component parameters.
			if(width==1 && (e==AIN_STRING || e==AIN_STRUCT || e==AIN_REF_STRUCT || e==AIN_ARRAY || e==AIN_REF_ARRAY)) {
				stack_push(-1);return true;
			}
			VM_ERROR("Array.%s index %d outside %d", name, index, count);
		}
		// Generic references to scalars are (array page, element index).
		// Objects/strings/arrays use their heap slot directly.
		if (e == AIN_INT || e == AIN_FLOAT || e == AIN_BOOL || e == AIN_ENUM) {
			heap_ref(slot);
			stack_push(slot); stack_push(index);
		} else if (width == 1) {
			heap_ref(p->values[index].i);
			stack_push(p->values[index]);
		} else {
			heap_ref(p->values[index*2].i);
			stack_push(p->values[index*2]); stack_push(p->values[index*2+1]);
		}
	} else if (!strcmp(name, "Duplicate") && n == 2) {
		int source = stack_pop().i;
		stack_pop();
		struct page *copy = copy_page(heap_get_page(source));
		delete_page_vars(p); free_page(p); heap_set_page(slot, copy);
	} else if (!strcmp(name,"Copy") && n>=2 && n<=5) {
		int length=INT32_MAX,source_index=0,destination_index=0,source;
		if(n>=4) {length=stack_pop().i;source_index=stack_pop().i;}
		source=stack_pop().i;
		if(n==3 || n==5) destination_index=stack_pop().i;
		stack_pop();
		struct page *src=heap_get_page(source);
		if(!src || !src->element_type || ain12_array_width(src)!=width || p->array.rank!=1 || src->array.rank!=1 ||
			src->element_type->data!=p->element_type->data)
			VM_ERROR("Incompatible generic Array.Copy types");
		if(length<0 || source_index<0 || destination_index<0) VM_ERROR("Invalid Array.Copy range");
		int available=src->nr_vars/width-source_index;
		length=max(0,min(length,min(available,count-destination_index)));
		union vm_value *snapshot=xmalloc(sizeof(*snapshot)*(length?length:1)*width);
		for(int i=0;i<length*width;i++) snapshot[i]=vm_copy(src->values[source_index*width+i],variable_type(src,source_index*width+i,NULL,NULL));
		for(int i=0;i<length*width;i++) {
			p=heap_get_page(slot);
			variable_set(p,destination_index*width+i,variable_type(p,destination_index*width+i,NULL,NULL),snapshot[i]);
		}
		free(snapshot);stack_push(length);
	} else if (!strcmp(name,"ShallowCopy") && n==1 && width==1 && p->array.rank==1) {
		struct ain_type element=*p->element_type;
		if(element.data!=AIN_STRUCT && element.data!=AIN_REF_STRUCT)
			VM_ERROR("ShallowCopy element type %d is not implemented",element.data);
		element.data=AIN_REF_STRUCT;
		struct page *result=alloc_page(ARRAY_PAGE,AIN_ARRAY,count);
		page_set_element_type(result,&element);result->array=p->array;
		for(int i=0;i<count;i++) {result->values[i]=p->values[i];heap_ref(result->values[i].i);}
		stack_pop();stack_push(heap_alloc_page(result));
	} else {
		return false;
	}
	(void)type_param;
	return true;
}
