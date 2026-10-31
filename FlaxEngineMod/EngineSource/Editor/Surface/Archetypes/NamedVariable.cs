// Copyright (c) Wojciech Figat. All rights reserved.

using System;
using System.Collections.Generic;
using FlaxEditor.GUI;
using FlaxEditor.Scripting;
using FlaxEditor.Surface.Elements;
using FlaxEngine;
using FlaxEngine.GUI;
using FlaxEngine.Utilities;

namespace FlaxEditor.Surface.Archetypes
{
    /// <summary>
    /// Contains archetypes for nodes from the Named Variable group.
    /// </summary>
    [HideInEditor]
    public static class NamedVariable
    {
        public const ushort SetTypeID = 1;
        public const ushort GetTypeID = 2;

        public static NodeArchetype[] Nodes =
        {
            // Set node: receives a value, name must be unique
            new NodeArchetype
            {
                TypeID = SetTypeID,
                Create = (id, context, arch, groupArch) => new NamedVariableSetNode(id, context, arch, groupArch),
                Title = "Set Named Variable",
                Description = "Sets the value of a named variable. Name must be unique.",
                Flags = NodeFlags.MaterialGraph,
                Size = new Float2(200, 60),
                DefaultValues = new object[]
                {
                    "MyVar",                // Name
                    typeof(float).FullName, // Type
                },
                Elements = new[]
                {
                    NodeElementArchetype.Factory.Text(2, 22, "Name:"),
                    NodeElementArchetype.Factory.TextBox(50, 22, 140, 18, -1, false),
                    NodeElementArchetype.Factory.Input(2, "Value", true, typeof(float), 0),
                }
            },

            // Get node: outputs a value, name must match an existing Set node
            new NodeArchetype
            {
                TypeID = GetTypeID,
                Create = (id, context, arch, groupArch) => new NamedVariableGetNode(id, context, arch, groupArch),
                Title = "Get Named Variable",
                Description = "Gets the value of a named variable. Name must match an existing Set node.",
                Flags = NodeFlags.MaterialGraph,
                Size = new Float2(200, 60),
                DefaultValues = new object[]
                {
                    "MyVar",                // Name
                    typeof(float).FullName, // Type
                    new Float2(200, 60),    // Size for ResizableSurfaceNode
                },
                Elements = new[]
                {
                    NodeElementArchetype.Factory.Text(2, 22, "Name:"),
                    NodeElementArchetype.Factory.ComboBox(50, 22, 140),
                    NodeElementArchetype.Factory.Output(0, "Value", typeof(float), 0),
                }
            },
        };

        /// <summary>
        /// Gets all unique set variable names from the surface.
        /// </summary>
        public static List<string> GetAllSetNames(VisjectSurfaceContext context)
        {
            var result = new List<string>();
            if (context == null) return result;

            foreach (var node in context.Nodes)
            {
                if (node is NamedVariableSetNode setNode && !string.IsNullOrEmpty(setNode.VariableName))
                {
                    result.Add(setNode.VariableName);
                }
            }
            return result;
        }
    }

    /// <summary>
    /// Base class for both Set/Get nodes.
    /// </summary>
    public abstract class NamedVariableNodeBase : SurfaceNode
    {
        protected bool _isUpdateLocked;

        protected NamedVariableNodeBase(uint id, VisjectSurfaceContext context, NodeArchetype arch, GroupArchetype groupArch)
            : base(id, context, arch, groupArch)
        {
        }

        public string VariableName
        {
            get => (string)Values[0];
            set => SetValue(0, value);
        }

        public ScriptType VariableType
        {
            get => TypeUtils.GetType((string)Values[1]);
            set => SetValue(1, value.TypeName);
        }
    }

    public sealed class NamedVariableSetNode : NamedVariableNodeBase
    {
        private TextBox _textBox;
        private string _lastCommittedName;
        protected override Color ArchetypeColor => new Color(0x18, 0xce, 0x52, 0xFF);

        public NamedVariableSetNode(uint id, VisjectSurfaceContext context, NodeArchetype arch, GroupArchetype groupArch)
            : base(id, context, arch, groupArch)
        {
            _lastCommittedName = VariableName;
        }

        public override void OnLoaded(SurfaceNodeActions action)
        {
            base.OnLoaded(action);

            _lastCommittedName = VariableName;

            if (_textBox == null)
            {
                for (int i = 0; i < Children.Count; i++)
                {
                    if (Children[i] is TextBox tb)
                    {
                        _textBox = tb;
                        _textBox.Text = VariableName;
                        _textBox.EditEnd += OnTextBoxEditEnd;
                        break;
                    }
                }
            }
        }

        private void OnTextBoxEditEnd()
        {
            if (_isUpdateLocked || _textBox == null) return;

            string newName = _textBox.Text;
            if (newName == _lastCommittedName) return;

            string oldName = _lastCommittedName;

            // Check duplicate names BEFORE applying changes
            if (!string.IsNullOrEmpty(newName))
            {
                bool isDuplicate = false;
                foreach (var node in Surface.Nodes)
                {
                    if (node != this && node is NamedVariableSetNode other && other.VariableName == newName)
                    {
                        isDuplicate = true;
                        break;
                    }
                }

                if (isDuplicate)
                {
                    Editor.LogWarning($"[NamedVariable] Duplicate variable name '{newName}'. Names must be unique. Reverting to '{oldName}'.");

                    // Revert UI text to old name, no data change
                    _isUpdateLocked = true;
                    _textBox.Text = oldName;
                    _isUpdateLocked = false;
                    return;
                }
            }

            // Disable internal Undo tracking during the batch update
            var undo = Surface?.Undo;
            bool undoWasEnabled = undo != null && undo.Enabled;
            if (undo != null) undo.Enabled = false;

            // Update Set node itself
            _isUpdateLocked = true;
            VariableName = newName;
            _isUpdateLocked = false;
            _lastCommittedName = newName;

            // Broadcast to all Get nodes
            if (!string.IsNullOrEmpty(oldName) && oldName != newName)
            {
                foreach (var node in Surface.Nodes)
                {
                    if (node is NamedVariableGetNode getNode)
                    {
                        getNode.OnSetRenamed(oldName, newName);
                    }
                }
            }

            // Restore Undo and push an empty action to consume the undo step
            if (undo != null)
            {
                undo.Enabled = undoWasEnabled;
                if (undoWasEnabled)
                {
                    undo.AddAction(new EmptyUndoAction());
                }
            }
        }

        public override void OnValuesChanged()
        {
            base.OnValuesChanged();
            if (Surface == null) return;

            string newName = VariableName;
            string oldName = _lastCommittedName;

            if (_textBox != null && _textBox.Text != newName)
            {
                _isUpdateLocked = true;
                _textBox.Text = newName;
                _isUpdateLocked = false;
            }

            if (string.IsNullOrEmpty(newName)) return;

            bool isDuplicate = false;
            foreach (var node in Surface.Nodes)
            {
                if (node != this && node is NamedVariableSetNode other && other.VariableName == newName)
                {
                    isDuplicate = true;
                    break;
                }
            }
            if (isDuplicate)
            {
                Editor.LogWarning($"[NamedVariable] Duplicate variable name '{newName}'. Names must be unique.");
            }

            _lastCommittedName = newName;
        }

        public override void ConnectionTick(Box box)
        {
            base.ConnectionTick(box);

            // Set input type changed, notify all Get nodes to sync their output type
            if (Surface != null)
            {
                foreach (var node in Surface.Nodes)
                {
                    if (node is NamedVariableGetNode getNode)
                    {
                        getNode.UpdateOutputType();
                    }
                }
            }
        }

        public override void OnPasted(System.Collections.Generic.Dictionary<uint, uint> idsMapping)
        {
            base.OnPasted(idsMapping);

            string name = VariableName;
            if (string.IsNullOrEmpty(name)) return;

            var existingNames = new HashSet<string>();
            foreach (var node in Surface.Nodes)
            {
                if (node != this && node is NamedVariableSetNode other)
                {
                    existingNames.Add(other.VariableName);
                }
            }

            if (existingNames.Contains(name))
            {
                int suffix = 1;
                string newName;
                do
                {
                    newName = name + "_" + suffix;
                    suffix++;
                } while (existingNames.Contains(newName));

                _isUpdateLocked = true;
                VariableName = newName;
                if (_textBox != null) _textBox.Text = newName;
                _lastCommittedName = newName;
                _isUpdateLocked = false;

                Editor.Log($"[NamedVariable] Pasted node renamed to '{newName}' to avoid conflict.");
            }
        }
    }

    public sealed class NamedVariableGetNode : ResizableSurfaceNode
    {
        private ComboBoxElement _combobox;
        private bool _isUpdateLocked;
        protected override Color ArchetypeColor => new Color(0x00, 0xAD, 0xFF, 0xFF);

        public string VariableName
        {
            get => (string)Values[0];
            set => SetValue(0, value);
        }

        public NamedVariableGetNode(uint id, VisjectSurfaceContext context, NodeArchetype arch, GroupArchetype groupArch)
            : base(id, context, arch, groupArch)
        {
            _sizeValueIndex = 2;
            AllowedResizeDirections = RESIZE_HORIZONTAL;
            _sizeMin = new Float2(100, 60);
        }

        private NamedVariableSetNode FindSetNode(string name)
        {
            if (string.IsNullOrEmpty(name) || Surface == null) return null;
            foreach (var node in Surface.Nodes)
            {
                if (node is NamedVariableSetNode setNode && setNode.VariableName == name)
                    return setNode;
            }
            return null;
        }

        public override void OnLoaded(SurfaceNodeActions action)
        {
            base.OnLoaded(action);
            if (Surface != null)
            {
                UpdateCombo();
            }
        }

        public override void OnValuesChanged()
        {
            base.OnValuesChanged();

            if (_combobox == null)
            {
                UpdateCombo();
            }
            else if (!_isUpdateLocked)
            {
                _isUpdateLocked = true;
                _combobox.SelectedItem = VariableName;
                _isUpdateLocked = false;
            }
        }

        protected override void OnSizeChanged()
        {
            base.OnSizeChanged();

            if (_combobox != null)
            {
                _combobox.Width = Mathf.Max(40, Width - 65);
            }
        }

        public void OnSetRenamed(string oldName, string newName)
        {
            if (VariableName == oldName)
            {
                _isUpdateLocked = true;
                VariableName = newName;
                _isUpdateLocked = false;
            }

            UpdateCombo();
        }

        public void UpdateCombo()
        {
            if (_isUpdateLocked || Surface == null) return;

            _isUpdateLocked = true;

            if (_combobox == null)
            {
                _combobox = GetChild<ComboBoxElement>();
                if (_combobox != null)
                {
                    _combobox.SelectedIndexChanged += OnSelectedChanged;
                }
            }

            if (_combobox != null)
            {
                string currentName = VariableName;
                var names = NamedVariable.GetAllSetNames(Context);

                _combobox.ClearItems();
                for (int i = 0; i < names.Count; i++)
                {
                    _combobox.AddItem(names[i]);
                }

                if (!string.IsNullOrEmpty(currentName) && names.Contains(currentName))
                {
                    _combobox.SelectedItem = currentName;
                }
                else
                {
                    _combobox.SelectedItem = null;
                    VariableName = string.Empty;
                }
            }

            _isUpdateLocked = false;

            // Sync output box type after combobox refresh
            UpdateOutputType();
        }

        private void OnSelectedChanged(ComboBox cb)
        {
            if (_isUpdateLocked) return;

            var selectedName = _combobox.SelectedItem;
            if (selectedName != VariableName)
            {
                // Disable Undo recording for this internal change
                var undo = Surface?.Undo;
                bool undoWasEnabled = undo != null && undo.Enabled;
                if (undo != null) undo.Enabled = false;

                VariableName = selectedName;

                if (undo != null) undo.Enabled = undoWasEnabled;

                UpdateOutputType();
            }
        }

        public void UpdateOutputType()
        {
            var outputBox = GetBox(0) as OutputBox;
            if (outputBox == null) return;

            var setNode = FindSetNode(VariableName);
            if (setNode != null)
            {
                var inputBox = setNode.GetBox(0) as InputBox;
                if (inputBox != null)
                {
                    if (inputBox.HasAnyConnection && inputBox.Connections[0].CurrentType != ScriptType.Null)
                    {
                        outputBox.CurrentType = inputBox.Connections[0].CurrentType;
                    }
                    else
                    {
                        outputBox.CurrentType = inputBox.CurrentType != ScriptType.Null
                            ? inputBox.CurrentType
                            : ScriptType.Object;
                    }
                    return;
                }
            }

            // No valid Set node, fall back to Object to prevent crash
            outputBox.CurrentType = ScriptType.Object;
        }
    }

    /// <summary>
    /// An empty undo action that does nothing. Used as a placeholder to absorb the Undo input
    /// without altering the graph state.
    /// </summary>
    public class EmptyUndoAction : IUndoAction
    {
        public string ActionString => "Empty";

        public void Do()
        {
        }

        public void Undo()
        {
        }

        public void Dispose()
        {
        }
    }
}
