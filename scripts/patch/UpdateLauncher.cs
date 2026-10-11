using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Windows.Forms;

internal static class UpdateLauncher {
    internal static string Extract() {
        string root = Path.Combine(Path.GetTempPath(), "NP3Update-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        string archive = Path.Combine(root, "update.zip");
        using (Stream source = Assembly.GetExecutingAssembly().GetManifestResourceStream("Payload.zip"))
        using (FileStream output = File.Create(archive)) { source.CopyTo(output); }
        string content = Path.Combine(root, "content");
        ZipFile.ExtractToDirectory(archive, content);
        string[] folders = Directory.GetDirectories(content);
        if (folders.Length != 1 || !File.Exists(Path.Combine(folders[0], "Apply-Patch.ps1"))) throw new InvalidDataException("更新包内容不完整。");
        return folders[0];
    }
    [STAThread]
    private static int Main(string[] args) {
        try {
            string package = Extract();
            if (args.Length == 2 && args[0] == "--verify-extraction") {
                // Engineering check: extract the embedded archive without touching an installation.
                File.WriteAllText(args[1], package, new UTF8Encoding(false));
                return 0;
            }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new UpdateForm(package));
            return 0;
        } catch (Exception error) {
            if (args.Length > 0) return 1;
            MessageBox.Show(error.Message, "Notepad3 更新包", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
    }
}

internal sealed class UpdateForm : Form {
    private readonly string package;
    private readonly TextBox folder = new TextBox();
    private readonly TextBox output = new TextBox();
    private readonly Button browse = new Button();
    private readonly Button install = new Button();
    private readonly Button restore = new Button();
    private readonly Label status = new Label();
    private bool working;

    internal UpdateForm(string packagePath) {
        package = packagePath;
        Text = "Notepad3 扩展更新包 · x64";
        Font = new Font("Microsoft YaHei UI", 9F);
        ClientSize = new Size(660, 420);
        MinimumSize = new Size(676, 459);
        StartPosition = FormStartPosition.CenterScreen;
        AutoScaleMode = AutoScaleMode.Dpi;
        Label heading = new Label {Text="升级已有 Notepad3，保留原配置", Font=new Font(Font.FontFamily,15F,FontStyle.Bold), AutoSize=true, Location=new Point(20,18)};
        Label instruction = new Label {Text="先保存文件并退出 Notepad3，然后选择含 Notepad3.exe 的原安装目录。", AutoSize=true, Location=new Point(20,60)};
        folder.SetBounds(20,92,510,28); folder.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;
        string common = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),"Notepad3");
        if(File.Exists(Path.Combine(common,"Notepad3.exe"))) folder.Text=common;
        browse.Text="选择目录…"; browse.SetBounds(540,90,100,30); browse.Anchor=AnchorStyles.Top|AnchorStyles.Right;
        install.Text="安装更新"; install.SetBounds(20,138,130,36);
        restore.Text="恢复原版"; restore.SetBounds(164,138,130,36);
        status.Text="准备就绪 · 仅支持 x64 Notepad3"; status.SetBounds(20,190,620,24); status.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;
        output.Multiline=true; output.ReadOnly=true; output.ScrollBars=ScrollBars.Vertical;
        output.SetBounds(20,220,620,142); output.Anchor=AnchorStyles.Top|AnchorStyles.Bottom|AnchorStyles.Left|AnchorStyles.Right;
        Label note=new Label {Text="Markdown 预览需要 WebView2 Runtime。备份保存在原安装目录中。",AutoSize=true,Location=new Point(20,384),Anchor=AnchorStyles.Bottom|AnchorStyles.Left};
        Controls.AddRange(new Control[]{heading,instruction,folder,browse,install,restore,status,output,note});
        browse.Click += delegate {
            using(FolderBrowserDialog dialog=new FolderBrowserDialog()) {
                dialog.Description="选择原 Notepad3 安装目录"; dialog.ShowNewFolderButton=false;
                if(Directory.Exists(folder.Text)) dialog.SelectedPath=folder.Text;
                if(dialog.ShowDialog(this)==DialogResult.OK) folder.Text=dialog.SelectedPath;
            }
        };
        install.Click += delegate { Run(false); };
        restore.Click += delegate { Run(true); };
        FormClosing += delegate(object sender,FormClosingEventArgs e) {
            if(working) { e.Cancel=true; MessageBox.Show(this,"更新或恢复正在进行，请等待完成。","请稍候"); }
        };
    }
    private void Run(bool restoring) {
        string target;
        try {
            target=Path.GetFullPath(folder.Text.Trim());
            if(!File.Exists(Path.Combine(target,"Notepad3.exe"))) throw new IOException("此目录中没有 Notepad3.exe，请重新选择。");
        } catch(Exception error) { MessageBox.Show(this,error.Message,"目录无效",MessageBoxButtons.OK,MessageBoxIcon.Warning); return; }
        working=true; browse.Enabled=install.Enabled=restore.Enabled=folder.Enabled=false;
        status.Text=restoring?"正在恢复原版…":"正在备份并安装更新…"; output.Clear();
        BackgroundWorker worker=new BackgroundWorker();
        worker.DoWork += delegate(object sender,DoWorkEventArgs e) {
            string executable=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),@"WindowsPowerShell\v1.0\powershell.exe");
            ProcessStartInfo info=new ProcessStartInfo(executable);
            info.Arguments="-NoProfile -ExecutionPolicy Bypass -File \""+Path.Combine(package,"Apply-Patch.ps1")+"\" -NonInteractive -TargetDirectory \""+target.TrimEnd('\\')+"\\.\""+(restoring?" -Restore":"");
            info.UseShellExecute=false; info.CreateNoWindow=true;
            info.RedirectStandardOutput=true; info.RedirectStandardError=true;
            StringBuilder text=new StringBuilder();
            using(Process process=new Process()) {
                process.StartInfo=info;
                DataReceivedEventHandler capture=delegate(object s,DataReceivedEventArgs line) { if(line.Data!=null) lock(text) {text.AppendLine(line.Data);} };
                process.OutputDataReceived+=capture; process.ErrorDataReceived+=capture;
                process.Start(); process.BeginOutputReadLine(); process.BeginErrorReadLine(); process.WaitForExit();
                string log=text.ToString(); File.WriteAllText(Path.Combine(package,"last-operation.log"),log,new UTF8Encoding(true));
                e.Result=new object[]{process.ExitCode,log};
            }
        };
        worker.RunWorkerCompleted += delegate(object sender,RunWorkerCompletedEventArgs e) {
            working=false; browse.Enabled=install.Enabled=restore.Enabled=folder.Enabled=true;
            if(e.Error!=null) {status.Text="操作失败";output.Text=e.Error.Message;}
            else {
                object[] result=(object[])e.Result; output.Text=(string)result[1];
                status.Text=(int)result[0]==0?(restoring?"已恢复原版，配置保留。":"更新完成！请从原快捷方式启动 Notepad3。"):"操作未完成，请查看下方原因。";
            }
            worker.Dispose();
        };
        worker.RunWorkerAsync();
    }
}
