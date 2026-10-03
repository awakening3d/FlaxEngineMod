using System;
using FlaxEditor;
using FlaxEditor.CustomEditors;
using FlaxEditor.CustomEditors.Elements;
using FlaxEngine;
using FlaxEngine.GUI;

namespace FlaxEditor.Tools.Terrain.Paint
{
    public class TerrainSplatmapImportEditor : CustomEditor
    {
        public override DisplayStyle Style => DisplayStyle.Inline;

        public override void Initialize(LayoutElementsContainer layout)
        {
            var row = layout.HorizontalPanel();
            row.ContainerControl.Height = 20;

            var button = row.Button("import...");
            button.Button.Width = 80;
            button.Button.Clicked += OnImportClicked;
        }


        private SingleLayerMode GetOwner()
        {
            CustomEditor editor = this;   // 关键：显式声明为基类
            while (editor != null)
            {
                var values = editor.Values;
                if (values != null && values.Count > 0)
                {
                    if (values[0] is SingleLayerMode mode)
                        return mode;
                }
                editor = editor.ParentEditor;
            }
            return null;
        }

        private void OnImportClicked()
        {
            SingleLayerMode owner = GetOwner();
            if (owner == null) return;

            string initialFolder = null;
            if (!string.IsNullOrEmpty(owner.SplatmapImport) && System.IO.File.Exists(owner.SplatmapImport))
                initialFolder = System.IO.Path.GetDirectoryName(owner.SplatmapImport);

            const string filter = "Image files (*.png;*.tga;*.raw;*.exr)\0*.png;*.tga;*.raw;*.exr\0All files (*.*)\0*.*\0";

            // 返回值 true = 取消，false = 成功
            if (FileSystem.ShowOpenFileDialog(
                    Editor.Instance.Windows.MainWindow,
                    initialFolder,
                    filter,
                    false,
                    "Select single-channel splatmap",
                    out var files))
            {
                return;   // 取消
            }

            if (files == null || files.Length == 0) return;

            var path = files[0];
            SetValue(path);   // 记录路径，下次定位

            // 执行导入
            string error = TerrainSplatmapImporter.Import(owner, path);
            if (!string.IsNullOrEmpty(error))
            {
                MessageBox.Show(
                    Editor.Instance.Windows.MainWindow,
                    "Import failed:\n" + error,
                    "Splatmap Import Error",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
            }
        }
    }
}
