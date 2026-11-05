/* Recognize entered, balanced Parts input loops from their bytecode.
 * No game function names, frame/page addresses or saved-data changes.
 * Only an unconditional Begin followed by one matching End is accepted. */
struct vm_parts_scope {
    int function, crc;
    uint32_t begin_after, end_call;
    bool valid;
};
static struct vm_parts_scope vm_parts_scopes[128];
static unsigned vm_parts_scopes_used, vm_parts_scopes_next;
static struct ain *vm_parts_scope_program;
static const uint8_t *vm_parts_scope_code;

static bool vm_parts_scope_instruction(uint32_t at, int *op, uint32_t *next)
{
    if (at>=(uint32_t)ain->code_size || (uint32_t)ain->code_size-at<2)
        return false;
    *op=LittleEndian_getW(ain->code,at);
    if (*op<0 || *op>=NR_OPCODES)
        return false;
    unsigned width=instruction_width(*op);
    if (width>(uint32_t)ain->code_size-at)
        return false;
    *next=at+width;
    return true;
}

/* 1=Begin, -1=End, 0=unrelated/unknown. */
static int vm_parts_scope_hll(uint32_t at)
{
    int lib=LittleEndian_getDW(ain->code,at+2);
    int fun=LittleEndian_getDW(ain->code,at+6);
    if (lib<0 || lib>=ain->nr_libraries ||
            strcmp(ain->libraries[lib].name,"PartsEngine"))
        return 0;
    struct ain_library *l=&ain->libraries[lib];
    if (fun<0 || fun>=l->nr_functions)
        return 0;
    struct ain_hll_function *f=&l->functions[fun];
    if (f->nr_arguments || f->return_type.data!=AIN_VOID)
        return 0;
    if (!strcmp(f->name,"BeginInput"))return 1;
    if (!strcmp(f->name,"EndInput"))return -1;
    return 0;
}

static int vm_parts_scope_call(uint32_t at, int op)
{
    if (op==CALLHLL)return vm_parts_scope_hll(at);
    if (op!=CALLFUNC)return 0;
    int fno=LittleEndian_getDW(ain->code,at+2);
    if (fno<0 || fno>=ain->nr_functions)return 0;
    struct ain_function *f=&ain->functions[fno];
    if (f->nr_args || f->return_type.data!=AIN_VOID)return 0;
    int first,second,third;uint32_t after,tail,finish;
    if (!vm_parts_scope_instruction(f->address,&first,&after) || first!=CALLHLL ||
            !vm_parts_scope_instruction(after,&second,&tail) || second!=RETURN ||
            !vm_parts_scope_instruction(tail,&third,&finish) || third!=ENDFUNC ||
            LittleEndian_getDW(ain->code,tail+2)!=fno)
        return 0;
    return vm_parts_scope_hll(f->address);
}

static struct vm_parts_scope vm_parts_scope_parse(int fno)
{
    struct ain_function *f=&ain->functions[fno];
    struct vm_parts_scope out={.function=fno,.crc=f->crc};
    bool begun=false,ended=false;
    uint32_t start=f->address,at=start,next;
    while (at>=(uint32_t)f->address && at-start<65536) {
        int op;
        if (!vm_parts_scope_instruction(at,&op,&next))return out;
        if (op==ENDFUNC) {
            out.valid=begun && ended && LittleEndian_getDW(ain->code,at+2)==fno;
            return out;
        }
        int kind=vm_parts_scope_call(at,op);
        if (kind==1) {
            if (begun || ended)return out;
            begun=true;out.begin_after=next;
        } else if (kind==-1) {
            if (!begun || ended)return out;
            ended=true;out.end_call=at;
        } else if (!begun) {
            // A branch before Begin could skip it. Do not infer such a scope.
            if (op==RETURN || op==SJUMP || op==CALLONJUMP)return out;
            for (int i=0;i<instructions[op].nr_args;i++)
                if (instructions[op].args[i]==T_ADDR || instructions[op].args[i]==T_SWITCH)
                    return out;
        }
        at=next;
    }
    return out;
}

unsigned vm_parts_active_input_scopes(void)
{
    if (!ain || ain->version<11 || !ain->code ||
            call_stack_ptr<=0 || call_stack_ptr>4096)
        return 0;
    if (vm_parts_scope_program!=ain || vm_parts_scope_code!=ain->code) {
        vm_parts_scope_program=ain;vm_parts_scope_code=ain->code;
        vm_parts_scopes_used=vm_parts_scopes_next=0;
    }
    unsigned count=0;
    for (int i=0;i<call_stack_ptr;i++) {
        int fno=call_stack[i].fno;
        if (fno<0 || fno>=ain->nr_functions)continue;
        struct vm_parts_scope *s=NULL;
        for (unsigned j=0;j<vm_parts_scopes_used;j++)
            if (vm_parts_scopes[j].function==fno && vm_parts_scopes[j].crc==ain->functions[fno].crc) {
                s=&vm_parts_scopes[j];break;
            }
        if (!s) {
            unsigned n=vm_parts_scopes_used<128?vm_parts_scopes_used++:vm_parts_scopes_next++%128;
            vm_parts_scopes[n]=vm_parts_scope_parse(fno);s=&vm_parts_scopes[n];
        }
        uint32_t pc=i+1<call_stack_ptr?call_stack[i+1].call_address:(uint32_t)instr_ptr;
        if (s->valid && pc>=s->begin_after && pc<s->end_call)count++;
    }
    return count;
}
