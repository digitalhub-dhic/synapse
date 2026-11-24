schtasks /create /tn "SynapseGitAutoSync" /tr "pythonw.exe C:\Users\administrator\projects\synapse\watch_app_updates.pyw" /sc onlogon /rl highest /f
schtasks /run /tn "SynapseGitAutoSync"
