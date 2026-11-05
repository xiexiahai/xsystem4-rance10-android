#ifndef AIN12_ARRAY_ERASE_ALL_INC
#define AIN12_ARRAY_ERASE_ALL_INC

/* Decide first, publish one compacted page, then release removed values.
 * An IFACE is (owned object, unowned vtable), never two owned objects. */
static bool ain12_array_erase_all(struct ain_hll_function *f)
{
    if(!f || strcmp(f->name,"EraseAll") || f->nr_arguments!=2 ||
       f->arguments[0].type.data!=AIN_REF_ARRAY || f->arguments[1].type.data!=AIN_HLL_FUNC ||
       f->return_type.data!=AIN_BOOL)return false;
    int function=stack_pop().i,object=stack_pop().i,slot=stack_pop().i;
    if(function<0 || function>=ain->nr_functions || ain->functions[function].return_type.data!=AIN_BOOL)
        VM_ERROR("Array.EraseAll needs a bool predicate");
    if(!page_index_valid(slot))VM_ERROR("Array.EraseAll has no receiver");
    struct page *src=heap_get_page(slot);
    if(!src || src->type!=ARRAY_PAGE || src->a_type!=AIN_ARRAY || src->array.rank!=1 || !src->element_type)
        VM_ERROR("Array.EraseAll needs a rank-one generic array");
    int width=ain12_array_width(src);
    if(src->nr_vars<0 || src->nr_vars%width || src->nr_vars/width>10000000 ||
       !ain12_where_type_supported(src->element_type,0))VM_ERROR("Unsupported Array.EraseAll element shape");
    int count=src->nr_vars/width,parent=local_page_slot(),structure=src->array.struct_type;
    struct ain_type *type=ain12_where_type_copy(src->element_type,0);
    bool *remove=xcalloc(count?count:1,sizeof(*remove));int keep=0;
    for(int i=0;i<count;i++) {
        remove[i]=!!ain12_array_predicate(slot,i,object,function,parent);
        bool valid=page_index_valid(slot);
        src=valid?heap_get_page(slot):NULL;
        if(!src || src->type!=ARRAY_PAGE || src->a_type!=AIN_ARRAY || src->array.rank!=1 ||
           src->array.struct_type!=structure || src->nr_vars!=count*width || !src->element_type ||
           ain12_array_width(src)!=width || !ain12_where_type_equal(type,src->element_type,0)) {
            free(remove);ain12_where_type_free(type);
            VM_ERROR("Array.EraseAll callback changed the receiver shape");
        }
        keep+=!remove[i];
    }
    ain12_where_type_free(type);
    if(keep==count){free(remove);stack_push(false);return true;}
    struct page *next=alloc_page(ARRAY_PAGE,AIN_ARRAY,keep*width);
    page_set_element_type(next,src->element_type);next->array=src->array;
    int out=0;
    for(int i=0;i<count;i++) if(!remove[i]) {
        for(int k=0;k<width;k++)next->values[out*width+k]=src->values[i*width+k];
        src->values[i*width].i=-1;
        if(width==2)src->values[i*width+1].i=0;
        out++;
    }
    heap_set_page(slot,next);free(remove);
    /* Removed destructors see the complete new receiver. Moved values in the
     * old page are null, so only removed ownership is released here. */
    delete_page_vars(src);free_page(src);
    stack_push(true);return true;
}
#endif
