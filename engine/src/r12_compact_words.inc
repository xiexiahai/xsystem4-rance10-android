/* Save-time representation only: large int32 word arrays use one canonical
 * Raw JSON value instead of one cJSON node per integer. On-disk bytes and
 * history remain unchanged. Ordinary file parsing still produces Array nodes;
 * the Raw scanner only consumes the internal representation built here. */
static size_t r12_decimal_width(int32_t value)
{
    uint32_t n=value<0?0u-(uint32_t)value:(uint32_t)value;
    size_t digits=n<10?1:n<100?2:n<1000?3:n<10000?4:n<100000?5:
        n<1000000?6:n<10000000?7:n<100000000?8:n<1000000000?9:10;
    return digits+(value<0);
}
static char *r12_decimal_write(char *out,int32_t value)
{
    uint32_t n=value<0?0u-(uint32_t)value:(uint32_t)value;
    char digits[10];unsigned count=0;
    if(value<0)*out++='-';
    do{digits[count++]=(char)('0'+n%10);n/=10;}while(n);
    while(count)*out++=digits[--count];
    return out;
}
static cJSON *r12_compact_words(const union vm_value *values,int count)
{
    if(count<256)return r12_words(values,count);
    if(!values || (size_t)count>R12_WORD_LIMIT)return NULL;
    size_t length=2;
    for(int i=0;i<count;i++) {
        size_t added=r12_decimal_width(values[i].i)+(i!=0);
        if(added>R12_FILE_LIMIT-length)return NULL;
        length+=added;
    }
    char *text=malloc(length+1);if(!text)return NULL;
    char *end=text;*end++='[';
    for(int i=0;i<count;i++) {
        if(i)*end++=',';
        end=r12_decimal_write(end,values[i].i);
    }
    *end++=']';*end=0;
    cJSON *node=cJSON_CreateRaw(text);free(text);
    /* Internal allocation hint only. Copy still checks all syntax/values and the exact count. */
    if(node)node->valueint=count;
    return node;
}
static void r12_compact_space(const char **at)
{
    while(**at==' ' || **at=='\t' || **at=='\r' || **at=='\n')++*at;
}
static bool r12_compact_scan(const char *text,union vm_value *out,int capacity,int *count)
{
    if(!text || capacity<0)return false;
    const char *at=text;int n=0;r12_compact_space(&at);
    if(*at++!='[')return false;
    r12_compact_space(&at);
    if(*at!=']')for(;;) {
        if(n>=capacity)return false;
        bool negative=*at=='-';if(negative)at++;
        if(*at<'0' || *at>'9')return false;
        if(*at=='0' && at[1]>='0' && at[1]<='9')return false;
        uint32_t value=0,limit=negative?2147483648u:2147483647u;
        do {
            unsigned digit=(unsigned)(*at++-'0');
            if(value>(limit-digit)/10u)return false;
            value=value*10u+digit;
        }while(*at>='0' && *at<='9');
        if(out)out[n].i=negative?(value==2147483648u?INT32_MIN:-(int32_t)value):(int32_t)value;
        n++;r12_compact_space(&at);
        if(*at!=',')break;
        at++;r12_compact_space(&at);
    }
    if(*at++!=']')return false;
    r12_compact_space(&at);if(*at)return false;
    *count=n;return true;
}
static bool r12_compact_word_count(const cJSON *values,int *count)
{
    if(cJSON_IsArray(values)){*count=cJSON_GetArraySize(values);return *count>=0;}
    if(cJSON_IsRaw(values) && values->valueint>=256 && (unsigned)values->valueint<=R12_WORD_LIMIT) {
        *count=values->valueint;return true;
    }
    return cJSON_IsRaw(values) && r12_compact_scan(values->valuestring,NULL,R12_WORD_LIMIT,count);
}
static bool r12_compact_words_copy(const cJSON *values,union vm_value *out,int count)
{
    if(cJSON_IsArray(values)) {
        int k=0;const cJSON *v;
        cJSON_ArrayForEach(v,values)if(k>=count || !r12_int(v,&out[k++].i))return false;
        return k==count;
    }
    int n=0;
    return cJSON_IsRaw(values) && r12_compact_scan(values->valuestring,out,count,&n) && n==count;
}
