/* The verified Sunset 0.49 repair AIN handles the streamer unlock inside
 * Scenes::RunCharacterSetting. Enter its comparison and result blocks without
 * running the character editor or passing the phrase to its other commands. */
enum {
	SUNSET_PHRASE_BODY = 0x4579d2,
	SUNSET_PHRASE_COMPARE = 0x457b3e,
	SUNSET_PHRASE_REJECT = 0x458444,
	SUNSET_PHRASE_ACCEPT = 0x458486,
	SUNSET_PHRASE_END = 0x4584f2,
	SUNSET_PHRASE_MAX_BYTES = 256,
};

static const char *sunset_phrase_path;
static int sunset_phrase_handler;
static int sunset_phrase_title;
static int sunset_phrase_join;

static void sunset_phrase_init(void)
{
	sunset_phrase_path = NULL;
	const char *path = getenv("XSYSTEM4_SUNSET_PHRASE_FILE");
	if (!path || !*path || ain->code_size < SUNSET_PHRASE_END ||
	    crc32(0, ain->code + SUNSET_PHRASE_BODY,
		  SUNSET_PHRASE_END - SUNSET_PHRASE_BODY) != 0xd2c8156b)
		return;

	sunset_phrase_handler = ain_get_function(ain, "Scenes::RunCharacterSetting");
	sunset_phrase_title = ain_get_function(ain, "SceneTitle@Run");
	sunset_phrase_join = ain_get_function(ain, "SceneStack@Join");
	if (sunset_phrase_handler >= 0 && sunset_phrase_title >= 0 &&
	    sunset_phrase_join >= 0 && ain->functions[sunset_phrase_handler].nr_args == 0)
		sunset_phrase_path = path;
}

static enum opcode execute_instruction(enum opcode opcode);

static bool sunset_phrase_before_title_join(int method)
{
	if (!sunset_phrase_path || method != sunset_phrase_join ||
	    call_stack_ptr == 0 || call_stack[call_stack_ptr - 1].fno != sunset_phrase_title)
		return false;

	// Move the request out of the queue before reading, so a concurrent save
	// in the management activity can still submit a separate request.
	const char *path = sunset_phrase_path;
	sunset_phrase_path = NULL;
	char *claimed = xmalloc(strlen(path) + sizeof(".applying"));
	sprintf(claimed, "%s.applying", path);
	remove_utf8(claimed);
	if (rename(path, claimed) != 0) {
		free(claimed);
		return false;
	}
	FILE *request = file_open_utf8(claimed, "rb");
	char utf8[SUNSET_PHRASE_MAX_BYTES + 4] = {0};
	size_t length = request ? fread(utf8, 1, SUNSET_PHRASE_MAX_BYTES + 1, request) : 0;
	bool readable = request && !ferror(request);
	if (request)
		fclose(request);
	remove_utf8(claimed);
	free(claimed);

	size_t title_join_address = instr_ptr;
	bool accepted = false;
	if (readable && length > 0 && length <= SUNSET_PHRASE_MAX_BYTES &&
	    !memchr(utf8, '\0', length)) {
		char *phrase = utf2sjis(utf8, length);
		stack_push_string(cstr_to_string(phrase));
		free(phrase);

		// Use the MOD's embedded string and its own S_EQUALE instruction.
		instr_ptr = SUNSET_PHRASE_COMPARE;
		execute_instruction(S_PUSH);
		instr_ptr += instruction_width(S_PUSH);
		execute_instruction(S_EQUALE);
		accepted = stack_pop().i;
	}

	// Run the MOD's existing dialog and settings path in a normal VM frame.
	// Return to the original CALLMETHOD with its receiver and operands intact.
	instr_ptr = title_join_address;
	function_call(sunset_phrase_handler, title_join_address);
	instr_ptr = accepted ? SUNSET_PHRASE_ACCEPT : SUNSET_PHRASE_REJECT;
	return true;
}
