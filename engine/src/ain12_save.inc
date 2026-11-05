/* Typed AIN11+ GSAVE data. Runtime callbacks and VM continuation state are
 * outside this format. The caller retains atomic file replacement. */
static union vm_value ain12_gsave_value(struct gsave *save,const struct ain_type *type,int value,int depth);

static struct gsave_flat_array *ain12_gsave_array(struct gsave *save,int slot,
        struct gsave_array *array,struct gsave_flat_array *flat,int depth)
{
    struct page *p=heap_get_page(slot);int size=array->dimensions[p->array.rank-1];
    if(size<0 || size>10000000) VM_ERROR("Invalid typed save array size");
    heap_set_page(slot,generic_array_resize(p,size,false));p=heap_get_page(slot);
    if(p->array.rank>1) {
        for(int i=0;i<size;i++) flat=ain12_gsave_array(save,p->values[i].i,array,flat,depth+1);
        return flat;
    }
    if(p->element_type->data==AIN_IFACE) VM_ERROR("Interface GSAVE arrays are not implemented");
    if(flat>=array->flat_arrays+array->nr_flat_arrays || flat->nr_values!=size || flat->type!=p->element_type->data)
        VM_ERROR("Typed save array element or shape mismatch");
    for(int i=0;i<size;i++) {
        union vm_value loaded=ain12_gsave_value(save,p->element_type,flat->values[i].value,depth+1);
        variable_fini(p->values[i],p->element_type->data,false);p->values[i]=loaded;
    }
    return flat+1;
}

static union vm_value ain12_gsave_value(struct gsave *save,const struct ain_type *type,int value,int depth)
{
    if(depth>128) VM_ERROR("Typed save nesting limit exceeded");
    switch(type->data) {
    case AIN_ENUM: case AIN_ENUM2: return vm_int(value);
    case AIN_DELEGATE:
        return variable_initval(AIN_DELEGATE);
    case AIN_STRUCT: {
        if(value==-1) return vm_int(-1);
        if(value<0 || value>=save->nr_records || type->struc<0 || type->struc>=ain->nr_structures)
            VM_ERROR("Invalid typed save object index");
        struct gsave_record *rec=&save->records[value];
        if(rec->struct_index<0 || rec->struct_index>=save->nr_struct_defs)
            VM_ERROR("Invalid typed save structure definition");
        struct gsave_struct_def *definition=&save->struct_defs[rec->struct_index];
        struct ain_struct *st=&ain->structures[type->struc];
        if(strcmp(definition->name,st->name) || rec->nr_indices>definition->nr_fields)
            VM_ERROR("Typed save structure mismatch");
        int slot=alloc_struct(type->struc);struct page *p=heap_get_page(slot);
        for(int i=0;i<rec->nr_indices;i++) {
            int member=struct_member_index(st,definition->fields[i].name);
            if(member<0) VM_ERROR("Unknown typed save structure member");
            const struct ain_type *field=&st->members[member].type;
            if(field->data!=definition->fields[i].type) VM_ERROR("Typed save member type mismatch");
            int index=rec->indices[i];
            if(index<0 || index>=save->nr_keyvals) VM_ERROR("Invalid typed save member index");
            union vm_value loaded=ain12_gsave_value(save,field,save->keyvals[index].value,depth+1);
            variable_fini(p->values[member],field->data,false);p->values[member]=loaded;
        }
        return vm_int(slot);
    }
    case AIN_ARRAY: {
        if(value<0 || value>=save->nr_arrays || !type->array_type)
            VM_ERROR("Invalid typed save array index or descriptor");
        struct gsave_array *array=&save->arrays[value];
        int slot=variable_init(type).i;
        if(array->rank==-1) return vm_int(slot);
        if(array->rank!=type->rank || array->rank<1 || array->rank>4)
            VM_ERROR("Typed save array rank mismatch");
        struct gsave_flat_array *end=ain12_gsave_array(save,slot,array,array->flat_arrays,depth+1);
        if(end!=array->flat_arrays+array->nr_flat_arrays) VM_ERROR("Unexpected typed save array payload");
        return vm_int(slot);
    }
    default: return gsave_to_vm_value(save,type->data,type->struc,type->rank,value);
    }
}
