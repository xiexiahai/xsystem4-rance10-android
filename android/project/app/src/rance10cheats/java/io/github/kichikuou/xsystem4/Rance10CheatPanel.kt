package io.github.kichikuou.xsystem4

import android.app.Activity
import android.graphics.Color
import android.graphics.Rect
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import android.view.ViewGroup
import android.widget.Button
import android.widget.CheckBox
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

/** Rance03-style in-app MOD menu, with separate Rance10 native callbacks. */
class Rance10CheatPanel(
    private val activity: Activity,
    readFlags: () -> Int,
    writeFlags: (Int) -> Unit,
    readCapabilities: () -> Int,
    foodRerollReady: () -> Boolean,
    requestFoodReroll: () -> Boolean
) {
    private var root: FrameLayout? = null
    private var bubble: Button? = null
    private var panel: LinearLayout? = null
    private var status: TextView? = null
    private var foodReroll: Button? = null
    private var flagsReader: (() -> Int)? = readFlags
    private var flagsWriter: ((Int) -> Unit)? = writeFlags
    private var capsReader: (() -> Int)? = readCapabilities
    private var foodReadyReader: (() -> Boolean)? = foodRerollReady
    private var foodRerollAction: (() -> Boolean)? = requestFoodReroll
    private val toggles = mutableListOf<Pair<CheckBox, Int>>()
    private val handler = Handler(Looper.getMainLooper())
    private val insets = Rect()
    private var syncing = false

    private val tick = object : Runnable {
        override fun run() {
            if (root?.visibility != View.VISIBLE || panel?.visibility != View.VISIBLE) return
            refresh()
            handler.postDelayed(this, 500)
        }
    }

    init { install() }

    private fun dp(value: Int) = (value * activity.resources.displayMetrics.density).toInt()

    private fun background() = GradientDrawable().apply {
        setColor(Color.argb(235, 28, 33, 38))
        cornerRadius = dp(12).toFloat()
        setStroke(dp(1), Color.rgb(77, 198, 161))
    }

    private fun text(value: String, size: Float = 13f) = TextView(activity).apply {
        text = value
        textSize = size
        setTextColor(Color.WHITE)
        setPadding(dp(12), dp(6), dp(12), dp(6))
    }

    private fun install() {
        val overlay = FrameLayout(activity).apply {
            // Only the actual entry/menu children consume touches. Empty game
            // areas keep dispatching to the SDL view underneath this sibling.
            isClickable = false
            isFocusable = false
        }
        root = overlay
        activity.addContentView(overlay, FrameLayout.LayoutParams(-1, -1))
        val entry = Button(activity).apply {
            text = "CHEAT"
            textSize = 12f
            background = background()
            setTextColor(Color.WHITE)
        }
        bubble = entry
        overlay.addView(entry, FrameLayout.LayoutParams(dp(66), dp(44)).apply {
            leftMargin = dp(12)
            topMargin = dp(12)
        })
        val menu = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            background = background()
            elevation = dp(12).toFloat()
            visibility = View.GONE
            isClickable = true
        }
        panel = menu
        overlay.addView(menu, FrameLayout.LayoutParams(dp(390), dp(365)).apply {
            leftMargin = dp(12)
            topMargin = dp(12)
        })
        val heading = text("兰斯10 · CHEAT  拖动此处移动", 16f).apply {
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(Color.rgb(104, 232, 190))
        }
        menu.addView(heading)
        val scroll = ScrollView(activity)
        menu.addView(scroll, LinearLayout.LayoutParams(-1, 0, 1f))
        val options = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }
        scroll.addView(options)
        status = text("等待游戏就绪", 11f)
        options.addView(status)
        for ((label, bit) in listOf(
            "我方HP不减少" to 1,
            "行动不消耗AP" to 2,
            "宝箱概率100%" to 4,
            "食券使用不减／打火机重复使用" to 8,
            "黄色敌人开战前不限换队次数" to 16,
            "宝箱卡牌可逐张全领" to 64
        )) {
            val box = CheckBox(activity).apply {
                text = label
                textSize = 14f
                setTextColor(Color.WHITE)
                minHeight = dp(34)
                setPadding(dp(10), 0, dp(8), 0)
                isEnabled = false
            }
            toggles += box to bit
            box.setOnCheckedChangeListener { _, _ ->
                if (!syncing) {
                    val next = toggles.fold(0) { bits, (toggle, flag) ->
                        if (toggle.isChecked) bits or flag else bits
                    }
                    try {
                        flagsWriter?.invoke(next)
                    } catch (_: UnsatisfiedLinkError) {
                        // A mismatched/unavailable native variant stays disabled.
                    }
                    refresh()
                }
            }
            options.addView(box, LinearLayout.LayoutParams(-1, -2))
        }
        val reroll = Button(activity).apply {
            text = "食券 SL · 换一批"
            textSize = 14f
            isEnabled = false
            setOnClickListener {
                try {
                    foodRerollAction?.invoke()
                } catch (_: UnsatisfiedLinkError) {
                    // A mismatched native build keeps the button disabled.
                }
                refresh()
            }
        }
        foodReroll = reroll
        options.addView(reroll, LinearLayout.LayoutParams(-1, -2))
        options.addView(text("全部隐藏后，长按音量＋恢复入口。", 11f))
        val footer = LinearLayout(activity).apply { gravity = Gravity.END }
        val collapse = Button(activity).apply {
            text = "收起"
            textSize = 12f
            setOnClickListener {
                handler.removeCallbacks(tick)
                menu.visibility = View.GONE
                entry.visibility = View.VISIBLE
                fitToWindow()
            }
        }
        val hide = Button(activity).apply {
            text = "全部隐藏"
            textSize = 12f
            setOnClickListener {
                handler.removeCallbacks(tick)
                overlay.visibility = View.GONE
                Toast.makeText(activity, "长按音量＋可重新显示 MOD", Toast.LENGTH_LONG).show()
            }
        }
        footer.addView(collapse, LinearLayout.LayoutParams(0, dp(40), 1f))
        footer.addView(hide, LinearLayout.LayoutParams(0, dp(40), 1f))
        // Explicit full width keeps weighted footer buttons visible, as in R3.
        menu.addView(footer, LinearLayout.LayoutParams(-1, -2))
        entry.setOnClickListener {
            entry.visibility = View.GONE
            menu.visibility = View.VISIBLE
            fitToWindow()
            refresh()
            handler.removeCallbacks(tick)
            handler.postDelayed(tick, 500)
        }
        draggable(entry, entry)
        draggable(heading, menu)
        overlay.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ -> fitToWindow() }
        menu.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ -> fitToWindow() }
        overlay.setOnApplyWindowInsetsListener { _, windowInsets ->
            var left = max(0, windowInsets.systemWindowInsetLeft)
            var top = max(0, windowInsets.systemWindowInsetTop)
            var right = max(0, windowInsets.systemWindowInsetRight)
            var bottom = max(0, windowInsets.systemWindowInsetBottom)
            if (Build.VERSION.SDK_INT >= 28) {
                windowInsets.displayCutout?.let { cutout ->
                    left = max(left, cutout.safeInsetLeft)
                    top = max(top, cutout.safeInsetTop)
                    right = max(right, cutout.safeInsetRight)
                    bottom = max(bottom, cutout.safeInsetBottom)
                }
            }
            insets.set(left, top, right, bottom)
            fitToWindow()
            windowInsets
        }
        overlay.requestApplyInsets()
    }

    private fun bounds(): Rect? {
        val overlay = root ?: return null
        if (overlay.width <= 0 || overlay.height <= 0) return null
        val left = min(overlay.width - 1, insets.left + dp(12))
        val top = min(overlay.height - 1, insets.top + dp(12))
        return Rect(left, top,
            max(left + 1, overlay.width - insets.right - dp(12)),
            max(top + 1, overlay.height - insets.bottom - dp(12)))
    }

    private fun clamp(target: View, area: Rect) {
        val width = target.width.takeIf { it > 0 } ?: max(1, target.layoutParams.width)
        val height = target.height.takeIf { it > 0 } ?: max(1, target.layoutParams.height)
        target.x = target.x.coerceIn(area.left.toFloat(), max(area.left, area.right - width).toFloat())
        target.y = target.y.coerceIn(area.top.toFloat(), max(area.top, area.bottom - height).toFloat())
    }

    private fun fitToWindow() {
        val area = bounds() ?: return
        val menu = panel ?: return
        val width = min(dp(390), max(1, (area.width() * 0.92f).toInt()))
        val height = min(dp(365), max(1, (area.height() * 0.82f).toInt()))
        val lp = menu.layoutParams as FrameLayout.LayoutParams
        if (lp.width != width || lp.height != height) {
            lp.width = width
            lp.height = height
            menu.layoutParams = lp
        }
        bubble?.let { clamp(it, area) }
        clamp(menu, area)
    }

    private fun draggable(handle: View, target: View) {
        val slop = ViewConfiguration.get(activity).scaledTouchSlop
        var startX = 0f
        var startY = 0f
        var oldX = 0f
        var oldY = 0f
        var moved = false
        var activePointer = -1
        handle.setOnTouchListener { view, event ->
            fun release() {
                activePointer = -1
                view.isPressed = false
                view.parent?.requestDisallowInterceptTouchEvent(false)
            }
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    activePointer = event.getPointerId(0)
                    startX = event.rawX
                    startY = event.rawY
                    oldX = target.x
                    oldY = target.y
                    moved = false
                    view.isPressed = true
                    view.parent?.requestDisallowInterceptTouchEvent(true)
                }
                MotionEvent.ACTION_MOVE -> {
                    if (activePointer != -1) {
                        val dx = event.rawX - startX
                        val dy = event.rawY - startY
                        if (abs(dx) > slop || abs(dy) > slop) moved = true
                        if (moved) {
                            target.x = oldX + dx
                            target.y = oldY + dy
                            bounds()?.let { clamp(target, it) }
                        }
                    }
                }
                MotionEvent.ACTION_POINTER_UP -> {
                    if (event.getPointerId(event.actionIndex) == activePointer) release()
                }
                MotionEvent.ACTION_UP -> {
                    val click = activePointer != -1 && !moved
                    release()
                    if (click) view.performClick()
                }
                MotionEvent.ACTION_CANCEL -> release()
            }
            true
        }
    }

    fun refresh() {
        val state = try {
            val flags = flagsReader?.invoke() ?: 0
            val caps = capsReader?.invoke() ?: 0
            flags to caps
        } catch (_: UnsatisfiedLinkError) {
            0 to 0
        }
        syncing = true
        try {
            toggles.forEach { (box, bit) ->
                val checked = state.first and bit != 0
                val enabled = state.second and bit != 0
                if (box.isChecked != checked) box.isChecked = checked
                if (box.isEnabled != enabled) box.isEnabled = enabled
            }
            foodReroll?.isEnabled = try {
                state.second and 32 != 0 && foodReadyReader?.invoke() == true
            } catch (_: UnsatisfiedLinkError) {
                false
            }
            val message = if (state.second == 0) "等待游戏就绪" else "开关设置会自动保存"
            status?.let { if (it.text.toString() != message) it.text = message }
        } finally {
            syncing = false
        }
    }

    fun handleKey(event: KeyEvent): Boolean {
        if (root != null && event.keyCode == KeyEvent.KEYCODE_VOLUME_UP &&
            event.action == KeyEvent.ACTION_DOWN && event.repeatCount > 0) {
            root?.visibility = View.VISIBLE
            panel?.visibility = View.GONE
            bubble?.visibility = View.VISIBLE
            handler.removeCallbacks(tick)
            fitToWindow()
            return true
        }
        return false
    }

    fun detach() {
        handler.removeCallbacks(tick)
        (root?.parent as? ViewGroup)?.removeView(root)
        root = null
        bubble = null
        panel = null
        status = null
        foodReroll = null
        flagsReader = null
        flagsWriter = null
        capsReader = null
        foodReadyReader = null
        foodRerollAction = null
        toggles.clear()
        syncing = false
    }
}
