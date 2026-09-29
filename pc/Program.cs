using System;
using System.Drawing;
using System.Windows.Forms;

namespace YoshiOS.FinalStatus;

internal static class Program
{
    [STAThread]
    static void Main()
    {
        ApplicationConfiguration.Initialize();

        using var form = new Form
        {
            Text = "Yoshi OS — Mise à jour finale",
            ClientSize = new Size(650, 430),
            StartPosition = FormStartPosition.CenterScreen,
            FormBorderStyle = FormBorderStyle.FixedDialog,
            MaximizeBox = false,
            MinimizeBox = true
        };

        var title = new Label
        {
            Text = "Yoshi OS",
            Font = new Font("Segoe UI", 24, FontStyle.Bold),
            AutoSize = true,
            Location = new Point(35, 30)
        };

        var status = new Label
        {
            Text = "PROJET ABANDONNÉ — VERSION NON TERMINÉE",
            Font = new Font("Segoe UI", 13, FontStyle.Bold),
            AutoSize = true,
            Location = new Point(35, 90)
        };

        var text = new Label
        {
            Text = "Cette application est la version finale du programme de statut PC.\n\n" +
                   "Aucune nouvelle version de Yoshi OS ne sera installée.\n" +
                   "Les mises à jour et installations sont définitivement désactivées.\n" +
                   "Toutes les versions conservées sont considérées comme abandonnées.\n\n" +
                   "Version conservée : 1.0.1\n\n" +
                   "Le dépôt est conservé uniquement comme archive du projet.",
            Font = new Font("Segoe UI", 11),
            AutoSize = true,
            Location = new Point(35, 140)
        };

        var close = new Button
        {
            Text = "Fermer",
            Size = new Size(110, 38),
            Location = new Point(500, 350)
        };
        close.Click += (_, _) => form.Close();

        form.Controls.AddRange(new Control[] { title, status, text, close });
        Application.Run(form);
    }
}
