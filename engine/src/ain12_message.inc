/* Rance10's AIN12 omits MSGF. Its observed AFL entry accepts (id,text),
 * whereas legacy MSGF accepts (id,total,text). Never call default index 0.
 * This is a validated named compatibility entry, not a universal AIN12 ABI. */
static int ain_message_handler(const struct ain *program,int *arguments)
{
    *arguments=3;
    if(program->version<11 || program->MSGF.present) return program->msgf;
    int found=-1;
    for(int i=1;i<program->nr_functions;i++) {
        const struct ain_function *f=&program->functions[i];
        if(!f->name || strcmp(f->name,"AFL_Message_Message"))continue;
        if(found>=0 || f->address>=program->code_size || f->nr_args!=2 || f->nr_vars<2 || !f->vars ||
           f->return_type.data!=AIN_VOID || f->vars[0].type.data!=AIN_INT || f->vars[1].type.data!=AIN_STRING) return -2;
        found=i;
    }
    if(found<0)return -2;
    *arguments=2;return found;
}
