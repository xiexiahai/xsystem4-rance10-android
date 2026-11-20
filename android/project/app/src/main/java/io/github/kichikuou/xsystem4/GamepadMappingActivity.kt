package io.github.kichikuou.xsystem4

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.os.Bundle
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast

internal object Rance10GamepadMapping {
    private const val PREFS = "rance10-gamepad"
    private const val BINDINGS = "bindings"
    private const val COUNT = 17
    private const val GUIDE = 5

    // Order matches SDL's standard A..D-pad buttons and then L2/R2 in input.c.
    val editableActions = (0 until COUNT).filter { it != GUIDE }

    fun defaults() = IntArray(COUNT) { it }

    fun load(context: Context): IntArray {
        val stored = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getString(BINDINGS, null) ?: return defaults()
        val actions = stored.split(',').mapNotNull { it.toIntOrNull() }
        return if (actions.size == COUNT && actions.toSet() == (0 until COUNT).toSet() && actions[GUIDE] == GUIDE)
            actions.toIntArray() else defaults()
    }

    fun save(context: Context, bindings: IntArray): Boolean =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putString(BINDINGS, bindings.joinToString(",")).commit()

    fun engineValue(context: Context): String {
        val bindings = load(context)
        return if (bindings.contentEquals(defaults())) "" else bindings.joinToString(",")
    }
}

class GamepadMappingActivity : Activity() {
    private lateinit var bindings: IntArray
    private lateinit var physicalNames: Array<String>
    private lateinit var actionNames: Array<String>
    private val rows = mutableMapOf<Int, Button>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        title = getString(R.string.rance10_gamepad_mapping)
        bindings = savedInstanceState?.getIntArray("bindings") ?: Rance10GamepadMapping.load(this)
        physicalNames = resources.getStringArray(R.array.rance10_pad_physical)
        actionNames = resources.getStringArray(R.array.rance10_pad_actions)

        val screen = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            val inset = (16 * resources.displayMetrics.density).toInt()
            setPadding(inset, inset, inset, inset)
        }
        val list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        list.addView(TextView(this).apply { setText(R.string.rance10_pad_instructions) })
        for (action in Rance10GamepadMapping.editableActions) {
            val row = Button(this).apply {
                isAllCaps = false
                setOnClickListener { chooseButton(action) }
            }
            rows[action] = row
            list.addView(row)
        }
        screen.addView(ScrollView(this).apply { addView(list) }, LinearLayout.LayoutParams(-1, 0, 1f))
        val controls = LinearLayout(this)
        controls.addView(Button(this).apply {
            setText(R.string.rance10_pad_reset)
            setOnClickListener { bindings = Rance10GamepadMapping.defaults(); updateRows() }
        }, LinearLayout.LayoutParams(0, -2, 1f))
        controls.addView(Button(this).apply {
            setText(R.string.rance10_pad_save)
            setOnClickListener {
                if (Rance10GamepadMapping.save(this@GamepadMappingActivity, bindings)) {
                    Toast.makeText(context, R.string.rance10_pad_saved, Toast.LENGTH_SHORT).show()
                    finish()
                } else {
                    Toast.makeText(context, R.string.rance10_pad_save_failed, Toast.LENGTH_LONG).show()
                }
            }
        }, LinearLayout.LayoutParams(0, -2, 1f))
        screen.addView(controls)
        setContentView(screen)
        updateRows()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        outState.putIntArray("bindings", bindings)
        super.onSaveInstanceState(outState)
    }

    private fun chooseButton(action: Int) {
        val options = Rance10GamepadMapping.editableActions
        val selected = bindings.indexOf(action)
        AlertDialog.Builder(this)
            .setTitle(actionNames[action])
            .setSingleChoiceItems(options.map { physicalNames[it] }.toTypedArray(), options.indexOf(selected)) { dialog, index ->
                val physical = options[index]
                bindings[selected] = bindings[physical]
                bindings[physical] = action
                updateRows()
                dialog.dismiss()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun updateRows() {
        for ((action, row) in rows)
            row.text = getString(R.string.rance10_pad_binding, actionNames[action], physicalNames[bindings.indexOf(action)])
    }
}
