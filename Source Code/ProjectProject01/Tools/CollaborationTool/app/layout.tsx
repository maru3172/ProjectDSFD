import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "Daybook · 팀 기록실",
  description: "회의부터 다음 할 일까지, 날짜별로 이어지는 팀의 기록.",
  icons: {
    icon: "/favicon.svg",
    shortcut: "/favicon.svg",
  },
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="ko">
      <body className="antialiased">{children}</body>
    </html>
  );
}
