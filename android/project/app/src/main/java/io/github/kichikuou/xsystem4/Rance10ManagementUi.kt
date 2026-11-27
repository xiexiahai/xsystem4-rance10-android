package io.github.kichikuou.xsystem4

import android.app.Activity
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.graphics.drawable.StateListDrawable
import android.text.SpannableString
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.text.style.RelativeSizeSpan
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import kotlin.math.roundToInt

internal class Rance10ManagementUi(private val activity: Activity) {
    private val density = activity.resources.displayMetrics.density

    fun dp(value: Int) = (density * value).roundToInt()

    fun page(title: String, subtitle: String): LinearLayout {
        val content = column()
        content.setPadding(dp(20), dp(12), dp(20), dp(26))
        content.add(backButton(), gap = 0, width = ViewGroup.LayoutParams.WRAP_CONTENT)
        content.add(text(title, 30, bold = true).apply {
            if (android.os.Build.VERSION.SDK_INT >= 28) isAccessibilityHeading = true
        }, gap = 12)
        content.add(text(subtitle, 14, color = MUTED), gap = 4)

        val centered = object : FrameLayout(activity) {
            override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
                content.layoutParams.width = minOf(dp(700), MeasureSpec.getSize(widthMeasureSpec))
                super.onMeasure(widthMeasureSpec, heightMeasureSpec)
            }
        }.apply {
            addView(content, FrameLayout.LayoutParams(-1, -2, Gravity.TOP or Gravity.CENTER_HORIZONTAL))
        }
        activity.setContentView(ScrollView(activity).apply {
            setBackgroundColor(PAGE)
            isFillViewport = true
            addView(centered)
        })
        return content
    }

    fun card(): LinearLayout = column().apply {
        setPadding(dp(16), dp(14), dp(16), dp(14))
        background = shape(SURFACE, 18, BORDER)
    }

    fun text(label: String, size: Int, color: Int = INK, bold: Boolean = false) =
        TextView(activity).apply {
            text = label
            textSize = size.toFloat()
            setTextColor(color)
            if (bold) typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
        }

    fun status(): TextView = text("", 14, color = ACCENT).apply {
        accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
    }

    fun button(label: String, primary: Boolean = false, click: () -> Unit): Button =
        Button(activity).apply {
            text = label
            textSize = 15f
            isAllCaps = false
            minHeight = dp(48)
            minimumHeight = dp(48)
            setPadding(dp(12), dp(7), dp(12), dp(7))
            setTextColor(ColorStateList(
                arrayOf(intArrayOf(-android.R.attr.state_enabled), intArrayOf()),
                intArrayOf(MUTED, if (primary) PAGE else INK)
            ))
            background = controlBackground(primary)
            setOnClickListener { click() }
        }

    fun add(parent: LinearLayout, view: View, gap: Int = 8) = parent.add(view, gap)

    fun tool(label: String, detail: String, click: () -> Unit) = button(label, click = click).apply {
        gravity = Gravity.START or Gravity.CENTER_VERTICAL
        minHeight = dp(64)
        minimumHeight = dp(64)
        val caption = SpannableString("$label\n$detail")
        caption.setSpan(RelativeSizeSpan(.83f), label.length + 1, caption.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        caption.setSpan(ForegroundColorSpan(MUTED), label.length + 1, caption.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        text = caption
    }

    fun row(vararg views: View): LinearLayout = LinearLayout(activity).apply {
        orientation = LinearLayout.HORIZONTAL
        views.forEachIndexed { index, view ->
            addView(view, LinearLayout.LayoutParams(0, -1, 1f).apply {
                if (index > 0) marginStart = dp(8)
            })
        }
    }

    fun controlBackground(emphasis: Boolean, radius: Int = 11): RippleDrawable {
        val states = StateListDrawable().apply {
            addState(intArrayOf(-android.R.attr.state_enabled), shape(PAGE, radius, BORDER))
            addState(intArrayOf(android.R.attr.state_focused), shape(if (emphasis) ACCENT else PANEL, radius, INK, 2))
            addState(intArrayOf(), shape(if (emphasis) ACCENT else PANEL, radius, if (emphasis) ACCENT else BORDER))
        }
        return RippleDrawable(ColorStateList.valueOf(0x3090E8D5), states, null)
    }

    private fun backButton() = button("‹  返回") { activity.finish() }

    private fun column() = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }

    private fun LinearLayout.add(view: View, gap: Int, width: Int = -1) {
        addView(view, LinearLayout.LayoutParams(width, -2).apply { topMargin = dp(gap) })
    }

    private fun shape(fill: Int, radius: Int, stroke: Int, width: Int = 1) =
        GradientDrawable().apply {
            setColor(fill)
            cornerRadius = dp(radius).toFloat()
            setStroke(dp(width), stroke)
        }

    companion object {
        val PAGE = Color.rgb(17, 22, 33)
        val SURFACE = Color.rgb(26, 34, 48)
        val PANEL = Color.rgb(38, 49, 67)
        val BORDER = Color.rgb(61, 77, 99)
        val INK = Color.rgb(238, 243, 248)
        val MUTED = Color.rgb(177, 191, 206)
        val ACCENT = Color.rgb(122, 222, 193)
    }
}
