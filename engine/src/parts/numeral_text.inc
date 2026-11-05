/* Text numeral fonts retain PARTS_NUMERAL and share its bounded integer layout.
 * Only the style is serialized; generated glyph textures and cached font_size
 * pointers are recreated with the ordinary text renderer. */
static bool parts_numeral_update(struct parts *parts, struct parts_numeral *num);

bool parts_numeral_text_style_valid(const struct text_style *s)
{
	return s->face <= 65535 && isfinite(s->size) && s->size >= 1 && s->size <= 512
		&& isfinite(s->bold_width) && s->bold_width >= 0 && s->bold_width <= 16
		&& s->weight == (unsigned)(s->bold_width * 1000)
		&& isfinite(s->edge_left) && s->edge_left >= 0 && s->edge_left <= 16
		&& s->edge_up == s->edge_left && s->edge_right == s->edge_left && s->edge_down == s->edge_left
		&& s->color.a == 255 && s->edge_color.a == 255
		&& s->scale_x == 1 && s->space_scale_x == 1 && s->font_spacing == 0;
}

static bool parts_numeral_text_style_equal(const struct text_style *a, const struct text_style *b)
{
	return a->face == b->face && a->size == b->size && a->bold_width == b->bold_width
		&& a->edge_left == b->edge_left
		&& a->color.r == b->color.r && a->color.g == b->color.g && a->color.b == b->color.b
		&& a->edge_color.r == b->edge_color.r && a->edge_color.g == b->edge_color.g
		&& a->edge_color.b == b->edge_color.b;
}

static bool parts_numeral_text_font_init(struct parts_numeral_font *font)
{
	if (!parts_numeral_text_style_valid(&font->text_style)) return false;
	font->text_style.font_size = NULL;
	for (int i = 0; i < 12; i++) {
		char ch[2] = { i < 10 ? '0' + i : i == 10 ? '-' : ',', 0 };
		Rectangle canvas = gfx_text_char_texture_rect(&font->text_style, ch);
		if (canvas.w < 1 || canvas.w > 4096 || canvas.h < 1 || canvas.h > 4096) return false;
		gfx_init_texture_rgba(&font->cg[i], canvas.w, canvas.h, (SDL_Color){0,0,0,0});
		if (!font->cg[i].handle) return false;
		float advance = gfx_render_textf(&font->cg[i], -canvas.x, -canvas.y,
			ch, &font->text_style, false);
		if (!isfinite(advance) || advance <= 0 || advance > 4096) return false;
		font->text_offset[i] = (Point){canvas.x, canvas.y};
		font->text_advance[i] = (int)ceilf(advance);
	}
	return true;
}

static int parts_load_numeral_font_text(const struct text_style *style)
{
	if (!parts_numeral_text_style_valid(style)) return -1;
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *f = &parts_numeral_fonts[i];
		if (f->type == PARTS_NUMERAL_FONT_TEXT && parts_numeral_text_style_equal(style, &f->text_style))
			return f->cg[0].handle ? i : -1;
	}
	if (parts_nr_numeral_fonts >= 4096) return -1;
	/* Build off-table so a failed glyph never leaves a cache entry. */
	struct parts_numeral_font candidate = {.type=PARTS_NUMERAL_FONT_TEXT,.cg_no=-1,.text_style=*style};
	parts_numeral_font_init(&candidate);
	for (int i = 0; i < 12; i++) if (!candidate.cg[i].handle) {
		parts_numeral_font_fini(&candidate);
		return -1;
	}
	int no = parts_nr_numeral_fonts++;
	parts_numeral_fonts = xrealloc_array(parts_numeral_fonts, no, no + 1, sizeof(*parts_numeral_fonts));
	parts_numeral_fonts[no] = candidate;
	return no;
}

static bool parts_numeral_text_update(struct parts *parts, struct parts_numeral *num,
		struct parts_numeral_font *font, const uint8_t *chars, int count)
{
	int64_t pen = 0, left = 0, top = 0, right = 0;
	int64_t bottom = (int)ceilf(text_style_height(&font->text_style));
	for (int i = 0; i < count; i++) {
		int c = chars[i];
		Texture *ch = &font->cg[c];
		if (!ch->handle || ch->w < 1 || ch->h < 1) return false;
		left = min(left, pen + font->text_offset[c].x);
		right = max(right, pen + font->text_offset[c].x + ch->w);
		top = min(top, font->text_offset[c].y);
		bottom = max(bottom, font->text_offset[c].y + ch->h);
		pen += font->text_advance[c];
		if (i + 1 < count) pen += num->space;
	}
	right = max(right, pen);
	int64_t width = right - left, height = bottom - top;
	if (pen < 1 || pen > 4096 || width < 1 || width > 4096 || height < 1 || height > 4096) return false;
	Texture next = {0};
	gfx_init_texture_rgba(&next, width, height, (SDL_Color){0,0,0,0});
	if (!next.handle) return false;
	pen = 0;
	for (int i = 0; i < count; i++) {
		int c = chars[i];
		char ch[2] = {c < 10 ? '0' + c : c == 10 ? '-' : ',', 0};
		/* Use the text renderer's straight-alpha glyph copy, which discards
		 * transparent pixels and retains neighbouring ink under negative
		 * spacing. Alpha-blending a cached glyph onto a transparent canvas
		 * would premultiply its RGB before the later Parts sprite blend. */
		gfx_render_textf(&next, pen - left, -top, ch, &font->text_style, false);
		pen += font->text_advance[c];
		if (i + 1 < count) pen += num->space;
	}
	gfx_delete_texture(&num->common.texture);
	num->common.texture = next;
	parts_set_dims(parts, &num->common, width, height);
	parts_dirty(parts);
	return true;
}

bool parts_numeral_set_text_font(int parts_no, const struct text_style *style,
		const struct parts_numeral *config, int state)
{
	if (!parts_state_valid(--state)) return false;
	int old_count = parts_nr_numeral_fonts;
	int no = parts_load_numeral_font_text(style);
	if (no < 0) return false;
	struct parts *parts = parts_get(parts_no);
	struct parts_state *old = &parts->states[state];
	struct parts_numeral candidate = {.length=1,.font_no=no};
	if (config) candidate = *config;
	else if (old->type == PARTS_NUMERAL) candidate = old->num;
	candidate.font_no = no;
	candidate.common.texture = (Texture){0};
	if (!parts_numeral_update(parts, &candidate)) {
		gfx_delete_texture(&candidate.common.texture);
		if (no >= old_count) {
			parts_numeral_font_fini(&parts_numeral_fonts[no]);
			parts_nr_numeral_fonts--;
		}
		return false;
	}
	parts_state_reset(old, PARTS_NUMERAL);
	old->num = candidate;
	parts_dirty(parts);
	return true;
}

bool PE_SetNumeralFont(int parts_no, int type, int size, int r, int g, int b,
		float bold_weight, int edge_r, int edge_g, int edge_b, float edge_weight, int state)
{
	if (type < 0 || type > 65535 || size < 1 || size > 512
		|| r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255
		|| edge_r < 0 || edge_r > 255 || edge_g < 0 || edge_g > 255 || edge_b < 0 || edge_b > 255
		|| !isfinite(bold_weight) || bold_weight < 0 || bold_weight > 16
		|| !isfinite(edge_weight) || edge_weight < 0 || edge_weight > 16) return false;
	struct text_style style = default_text_style;
	style.face=type; style.size=size; style.bold_width=bold_weight; style.weight=(unsigned)(bold_weight*1000);
	style.color=(SDL_Color){r,g,b,255}; style.edge_color=(SDL_Color){edge_r,edge_g,edge_b,255};
	text_style_set_edge_width(&style,edge_weight);
	return parts_numeral_set_text_font(parts_no,&style,NULL,state);
}
