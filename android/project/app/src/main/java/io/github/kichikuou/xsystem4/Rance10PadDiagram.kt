package io.github.kichikuou.xsystem4

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Typeface
import android.view.Gravity
import android.view.ViewGroup
import android.widget.Button
import kotlin.math.roundToInt

internal class Rance10PadDiagram(
    context: Context,
    private val ui: Rance10ManagementUi,
    onKey: (Int) -> Unit
) : ViewGroup(context) {
    private data class Key(val button: Int, val label: String, val x: Float, val y: Float)

    private val keys = listOf(
        Key(15, "L2", .22f, .10f), Key(16, "R2", .78f, .10f),
        Key(9, "L1", .22f, .27f), Key(10, "R1", .78f, .27f),
        Key(11, "↑", .20f, .43f), Key(12, "↓", .20f, .75f),
        Key(13, "←", .095f, .59f), Key(14, "→", .305f, .59f),
        Key(4, "−", .425f, .48f), Key(6, "+", .575f, .48f),
        Key(7, "L3", .375f, .83f), Key(8, "R3", .625f, .83f),
        Key(0, "A", .80f, .75f), Key(1, "B", .905f, .59f),
        Key(2, "X", .695f, .59f), Key(3, "Y", .80f, .43f)
    )
    private val buttons = keys.associate { key ->
        key.button to Button(context).apply {
            text = key.label
            textSize = 12f
            typeface = Typeface.DEFAULT_BOLD
            gravity = Gravity.CENTER
            minWidth = 0
            minHeight = 0
            minimumWidth = 0
            minimumHeight = 0
            includeFontPadding = false
            setPadding(0, 0, 0, 0)
            setOnClickListener { onKey(key.button) }
            addView(this)
        }
    }
    private val outline = Path()
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)

    init {
        setWillNotDraw(false)
    }

    fun updateButton(physical: Int, label: String, customized: Boolean) {
        val button = buttons.getValue(physical)
        button.contentDescription = label
        button.background = ui.controlBackground(customized, radius = 24)
        button.setTextColor(if (customized) Rance10ManagementUi.PAGE else Rance10ManagementUi.INK)
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val width = MeasureSpec.getSize(widthMeasureSpec)
        val height = resolveSize(ui.dp(if (width < ui.dp(360)) 200 else 240), heightMeasureSpec)
        setMeasuredDimension(width, height)
        val keyWidth = minOf(ui.dp(44), (width * .10f).toInt(), (height * .15f).toInt())
        val keySpec = MeasureSpec.makeMeasureSpec(keyWidth, MeasureSpec.EXACTLY)
        for (button in buttons.values) button.measure(keySpec, keySpec)
    }

    override fun onLayout(changed: Boolean, left: Int, top: Int, right: Int, bottom: Int) {
        for (key in keys) {
            val button = buttons.getValue(key.button)
            val x = (width * key.x - button.measuredWidth / 2f).roundToInt()
            val y = (height * key.y - button.measuredHeight / 2f).roundToInt()
            button.layout(x, y, x + button.measuredWidth, y + button.measuredHeight)
        }
    }

    override fun onSizeChanged(width: Int, height: Int, oldWidth: Int, oldHeight: Int) {
        val w = width.toFloat()
        val h = height.toFloat()
        outline.rewind()
        outline.moveTo(.04f * w, .65f * h)
        outline.cubicTo(.07f * w, .30f * h, .10f * w, .19f * h, .24f * w, .18f * h)
        outline.cubicTo(.34f * w, .17f * h, .36f * w, .33f * h, .50f * w, .33f * h)
        outline.cubicTo(.64f * w, .33f * h, .66f * w, .17f * h, .76f * w, .18f * h)
        outline.cubicTo(.90f * w, .19f * h, .93f * w, .30f * h, .96f * w, .65f * h)
        outline.cubicTo(1.01f * w, .93f * h, .96f * w, 1.03f * h, .84f * w, .94f * h)
        outline.cubicTo(.71f * w, .85f * h, .64f * w, .91f * h, .50f * w, .91f * h)
        outline.cubicTo(.36f * w, .91f * h, .29f * w, .85f * h, .16f * w, .94f * h)
        outline.cubicTo(.04f * w, 1.03f * h, -.01f * w, .93f * h, .04f * w, .65f * h)
        outline.close()
    }

    override fun onDraw(canvas: Canvas) {
        paint.style = Paint.Style.FILL
        paint.color = Rance10ManagementUi.PAGE
        canvas.drawPath(outline, paint)
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = ui.dp(1).toFloat()
        paint.color = Rance10ManagementUi.BORDER
        canvas.drawPath(outline, paint)
    }
}
