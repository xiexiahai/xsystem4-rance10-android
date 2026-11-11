#ifndef AIN12_ARRAY_ERASE_SET_INC
#define AIN12_ARRAY_ERASE_SET_INC

/* Erase(ref array self, ref array removed) -> bool, observed INT/STRING forms.
 * Both arrays are borrowed. Membership removes all matching receiver values,
 * preserving survivor order; removed stays unchanged. This is the project's
 * set-difference contract for the observed ID-list consumers, not a claim of
 * native deep equality for structs/arrays/interfaces or multiset subtraction.
 */
static int ain12_erase_set_count(struct page *p, const char *which)
{
    if(!p || p->type!=ARRAY_PAGE || p->a_type!=AIN_ARRAY ||
       p->array.rank!=1 || !p->element_type || p->nr_vars<0 ||
       p->nr_vars>10000000 || ain12_array_width(p)!=1)
        VM_ERROR("Array.Erase(array) %s needs a bounded rank-one generic array",which);
    return p->nr_vars;
}

static bool ain12_array_erase_set(struct ain_hll_function *f,int type_param)
{
    if(!f || strcmp(f->name,"Erase") || f->nr_arguments!=2 || !f->arguments ||
       f->return_type.data!=AIN_BOOL ||
       f->arguments[0].type.data!=AIN_REF_ARRAY ||
       f->arguments[1].type.data!=AIN_REF_ARRAY)return false;
    if(stack_ptr<2)VM_ERROR("Array.Erase(array) has too few stack operands");
    int removed_slot=stack_peek(0).i,self_slot=stack_peek(1).i;
    if(!page_index_valid(self_slot) || !page_index_valid(removed_slot))
        VM_ERROR("Array.Erase(array) has an invalid array reference");
    struct page *self=heap_get_page(self_slot),*removed=heap_get_page(removed_slot);
    int count=ain12_erase_set_count(self,"self"),keys=ain12_erase_set_count(removed,"removed");
    enum ain_data_type type=self->element_type->data;
    if((type!=AIN_INT && type!=AIN_STRING) || type_param!=(int)type)
        VM_ERROR("Array.Erase(array) only supports matching INT/STRING type arguments");
    if(!ain12_where_type_equal(self->element_type,removed->element_type,0) ||
       self->array.struct_type!=removed->array.struct_type)
        VM_ERROR("Array.Erase(array) element descriptors differ");
    if(type==AIN_STRING) {
        for(int i=0;i<count;i++)if(self->values[i].i!=-1 && !string_index_valid(self->values[i].i))
            VM_ERROR("Array.Erase(array) self contains an invalid string");
        for(int i=0;i<keys;i++)if(removed->values[i].i!=-1 && !string_index_valid(removed->values[i].i))
            VM_ERROR("Array.Erase(array) removed contains an invalid string");
    }
    /* No callbacks or VM re-entry occur before publication. Borrowed scratch
     * values need neither vm_copy nor heap_ref, including self==removed. */
    stack_ptr-=2;
    if(!count || !keys){stack_push(false);return true;}
    struct page *lookup=alloc_page(ARRAY_PAGE,AIN_ARRAY,keys);
    bool has_null=false;int lookup_count=0;
    for(int i=0;i<keys;i++) {
        if(type==AIN_STRING && removed->values[i].i==-1){has_null=true;continue;}
        lookup->values[lookup_count++]=removed->values[i];
    }
    ain12_array_order_sort(lookup,lookup_count,type,false);
    bool *erase=xcalloc((size_t)count,sizeof(*erase));int keep=0;
    for(int i=0;i<count;i++) {
        if(type==AIN_STRING && self->values[i].i==-1)erase[i]=has_null;
        else {
            int lo=0,hi=lookup_count;
            while(lo<hi) {
                int mid=lo+(hi-lo)/2;
                if(ain12_array_order_compare(type,lookup->values[mid],self->values[i])<0)lo=mid+1;
                else hi=mid;
            }
            erase[i]=lo<lookup_count && !ain12_array_order_compare(type,lookup->values[lo],self->values[i]);
        }
        keep+=!erase[i];
    }
    /* Lookup owns no VM values. Do not delete_page_vars its borrowed slots. */
    free_page(lookup);
    if(keep==count){free(erase);stack_push(false);return true;}
    struct page *next=alloc_page(ARRAY_PAGE,AIN_ARRAY,keep);
    page_set_element_type(next,self->element_type);next->array=self->array;
    int out=0;
    for(int i=0;i<count;i++)if(!erase[i]) {
        next->values[out++]=self->values[i];
        self->values[i]=variable_initval(type);
    }
    heap_set_page(self_slot,next);free(erase);
    /* Transfer surviving owners, then release only removed string owners.
     * Array slot refcounts, including aliases, remain unchanged. */
    delete_page_vars(self);free_page(self);
    stack_push(true);return true;
}
#endif
