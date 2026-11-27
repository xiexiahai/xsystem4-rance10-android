package io.github.kichikuou.xsystem4

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.os.Bundle
import android.view.Gravity
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
    private lateinit var diagram: Rance10PadDiagram
    private lateinit var summary: TextView
    private var selectedButton: Int? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        bindings = savedInstanceState?.getIntArray("bindings") ?: Rance10GamepadMapping.load(this)
        selectedButton = savedInstanceState?.getInt("selected", -1)?.takeIf { it >= 0 }
        physicalNames = resources.getStringArray(R.array.rance10_pad_physical)
        actionNames = resources.getStringArray(R.array.rance10_pad_actions)

        val ui = Rance10ManagementUi(this)
        val page = ui.page(getString(R.string.rance10_gamepad_mapping), getString(R.string.rance10_pad_instructions))
        val card = ui.card()
        diagram = Rance10PadDiagram(this, ui) { chooseAction(it) }
        ui.add(card, diagram, 0)
        summary = ui.text("", 14, Rance10ManagementUi.ACCENT).apply {
            gravity = Gravity.CENTER
            accessibilityLiveRegion = TextView.ACCESSIBILITY_LIVE_REGION_POLITE
        }
        ui.add(card, summary)
        ui.add(card, ui.button("查看全部映射") { showBindings() }, 12)
        ui.add(page, card, 20)

        val reset = ui.button(getString(R.string.rance10_pad_reset)) {
            bindings = Rance10GamepadMapping.defaults()
            updateDiagram()
        }
        val save = ui.button(getString(R.string.rance10_pad_save), primary = true) {
            if (Rance10GamepadMapping.save(this, bindings)) {
                Toast.makeText(this, R.string.rance10_pad_saved, Toast.LENGTH_SHORT).show()
                finish()
            } else {
                Toast.makeText(this, R.string.rance10_pad_save_failed, Toast.LENGTH_LONG).show()
            }
        }
        ui.add(page, ui.row(reset, save), 14)
        updateDiagram()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        outState.putIntArray("bindings", bindings)
        outState.putInt("selected", selectedButton ?: -1)
        super.onSaveInstanceState(outState)
    }

    private fun chooseAction(physical: Int) {
        val options = Rance10GamepadMapping.editableActions
        selectedButton = physical
        updateDiagram()
        AlertDialog.Builder(this)
            .setTitle("为 ${physicalNames[physical]} 选择动作")
            .setSingleChoiceItems(options.map { actionNames[it] }.toTypedArray(), options.indexOf(bindings[physical])) { dialog, index ->
                val action = options[index]
                val otherButton = bindings.indexOf(action)
                bindings[otherButton] = bindings[physical]
                bindings[physical] = action
                updateDiagram()
                dialog.dismiss()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun showBindings() {
        val options = Rance10GamepadMapping.editableActions
        AlertDialog.Builder(this)
            .setTitle("当前映射")
            .setItems(options.map(::bindingLabel).toTypedArray()) { _, index -> chooseAction(options[index]) }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun bindingLabel(physical: Int) =
        getString(R.string.rance10_pad_binding, physicalNames[physical], actionNames[bindings[physical]])

    private fun updateDiagram() {
        for (physical in Rance10GamepadMapping.editableActions)
            diagram.updateButton(physical, bindingLabel(physical), bindings[physical] != physical)
        summary.text = selectedButton?.let(::bindingLabel) ?: "点选图中的实体键，查看或更改动作"
    }
}
